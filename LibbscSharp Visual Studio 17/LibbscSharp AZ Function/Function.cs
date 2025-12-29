using BscDotNet;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Azure.Functions.Worker;
using Microsoft.Extensions.Logging;
using System.IO.Compression;

namespace LibbscSharp_AZ_Function;

public class Function
{
    private readonly ILogger<Function> _logger;

    public Function(ILogger<Function> logger)
    {
        _logger = logger;
    }

    [Function("PING")]
    public IActionResult PING([HttpTrigger(AuthorizationLevel.Function, "get", "post")] HttpRequest req)
    {
        _logger.LogInformation("Ping trigger function.");
        return new OkObjectResult("Welcome to Azure Functions!");
    }

    [Function("COMPRESS")]
    public IActionResult COMPRESS([HttpTrigger(AuthorizationLevel.Function, "post")] HttpRequest req)
    {
        _logger.LogInformation("Compress function requested");
        try
        {
            // Read options
            // coder: 0=fast, 1=medium, 2=high (default: 1)
            bool hasCoder = int.TryParse(req.Query["coder"], out int coder);
            if (!hasCoder || coder < 1 || coder > 3)
                return new NotFoundObjectResult("Compression level unsupported. Accepted values are : 1, 2, 3.");

            // blockSize in MB (e.g., "100" → 100 MB), default 50 MB
            bool hasBlockSize = int.TryParse(req.Query["blockSize"], out int blockSizeMb);
            if (!hasBlockSize || blockSizeMb <= 0 || blockSizeMb > 150) // reasonable upper limit for available azure function ram
                blockSizeMb = 25;

            // 1. Read the full request body as byte[]
            byte[] inputData;
            using (var memoryStream = new MemoryStream())
            {
                req.Body.CopyToAsync(memoryStream).Wait();
                inputData = memoryStream.ToArray();
            }
            if (inputData.Length == 0)
            {
                return new NotFoundObjectResult("Empty request body.");
            }
            // Decompress gziped input
            if (inputData.Length >= 2   && inputData[0] == 0x1F  // 31
                                        && inputData[1] == 0x8B) // 139
            {
                try
                {
                    using var compressedStream = new MemoryStream(inputData);
                    using var gzipStream = new GZipStream(compressedStream, CompressionMode.Decompress);
                    using var decompressedStream = new MemoryStream();

                    gzipStream.CopyTo(decompressedStream);
                    inputData = decompressedStream.ToArray();
                }
                catch (InvalidDataException)
                {
                    return new BadRequestObjectResult("Invalid gzip data.");
                }
            }

            if (inputData.Length > 4 &&
            inputData[0] == (byte)'b' &&
            inputData[1] == (byte)'s' &&
            inputData[2] == (byte)'c' &&
            inputData[3] == (byte)'1')
            {
                return new BadRequestObjectResult("Input data is already compressed.");
            }
            // 2. Prepare input/output streams for your native compressor
            var inputStream = new MemoryStream(inputData, false); // read-only
            var outputStream = new MemoryStream();
            _logger.LogInformation($"Input file size : {inputStream.Length}");

            // 3. Call your native compressor
            int result = Compressor.CompressOmp(
                inputStream,
                outputStream,
                blockSizeMb * 1024 * 1024, // blocksize (ram consume about 2x block size per thread)
                NumThreads: 0, // 0 = auto (uses all available cores) Note that Azure Function basic plan only have one core
                lzpHashSize: 16,
                lzpMinLen: 128,
                blockSorter: 1, // Usualy best option (preceeding)
                coder // 0 = fast, 1 = medium, 2 = high
            );
            if (result < 0)
            {
                return new NotFoundObjectResult($"Error thrown during compression : {result}");
            }
            _logger.LogInformation($"Compression suceed to : {outputStream.Length}");

            // 4. Prepare successful response
            outputStream.Position = 0;
            return new FileStreamResult(outputStream, "application/octet-stream") { FileDownloadName = "compressed-data.bsc" };
        }
        catch (Exception ex)
        {
            return new NotFoundObjectResult($"Internal server error : {ex.Message}");
        }
    }

    [Function("DECOMPRESS")]
    public IActionResult DECOMPRESS([HttpTrigger(AuthorizationLevel.Function, "post")] HttpRequest req)
    {
        _logger.LogInformation("Decompress function requested");
        try
        {
            // 1. Read the full request body as byte[]
            byte[] inputData;
            using (var memoryStream = new MemoryStream())
            {
                req.Body.CopyToAsync(memoryStream).Wait();
                inputData = memoryStream.ToArray();
            }
            if (inputData.Length == 0)
            {
                return new NotFoundObjectResult("Empty request body.");
            }
            if (inputData.Length < 4 ||
            inputData[0] != (byte)'b' ||
            inputData[1] != (byte)'s' ||
            inputData[2] != (byte)'c' ||
            inputData[3] != (byte)'1')
            {
                return new BadRequestObjectResult("Input does not contain BSC compressed data, expected header 'bsc1'.");
            }
            // 2. Prepare input/output streams for your native compressor
            var inputStream = new MemoryStream(inputData, false); // read-only
            var outputStream = new MemoryStream();
            _logger.LogInformation($"Input file size : {inputStream.Length}");

            // 3. Call your native compressor
            int result = BscDotNet.Compressor.DecompressOmp(
                inputStream,
                outputStream,
                numThreads: 0 // 0 = auto (uses all available cores) Note that Azure Function basic plan only have one core
            );
            if (result < 0)
            {
                return new NotFoundObjectResult($"Error code thrown during decompression : {result}");
            }
            _logger.LogInformation($"Decompression suceed to : {outputStream.Length}");

            // 4. Prepare successful response
            outputStream.Position = 0;

            // Compress
            var acceptEncoding = req.Headers.AcceptEncoding.ToString(); // or req.Headers in controller
            if (acceptEncoding.Contains("gz", StringComparison.OrdinalIgnoreCase) ||
                acceptEncoding.Contains("gzip", StringComparison.OrdinalIgnoreCase))
            {
                var compressedStream = new MemoryStream(); // Or pipe directly if possible
                var gzip = new GZipStream(compressedStream, CompressionLevel.Fastest, leaveOpen: true);
                outputStream.CopyTo(gzip);
                compressedStream.Position = 0;

                // Now return the compressed version
                return new CompressedFileResult(compressedStream, "decompressed-data.gz");
            } else {
                return new FileStreamResult(outputStream, "application/octet-stream") { FileDownloadName = "decompressed-data" };
            }
        }
        catch (Exception ex)
        {
            return new NotFoundObjectResult($"Internal server error : {ex.Message}");
        }
    }
}


public class CompressedFileResult : FileStreamResult
{
    public CompressedFileResult(Stream fileStream, string fileDownloadName)
        : base(fileStream, "application/octet-stream")
    {
        FileDownloadName = fileDownloadName;
    }
    public override async Task ExecuteResultAsync(ActionContext context)
    {
        // Set required headers BEFORE the base writes the body
        var response = context.HttpContext.Response;
        response.Headers["Content-Encoding"] = "gzip";
        response.Headers["Vary"] = "Accept-Encoding";
        response.Headers.Remove("Content-Length"); // Important for compressed streams
        await base.ExecuteResultAsync(context);
    }
}