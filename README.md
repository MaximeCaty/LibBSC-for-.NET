
# LibBSC Sharp

**.NET port** of the excellent **Block Sorting Compression** library by Ilya Grebnov: [https://github.com/IlyaGrebnov/libbsc](https://github.com/IlyaGrebnov/libbsc?referrer=grok.com)

-   Targets **.NET 8** and **.NET Framework 4.8**
-   Include **Azure Function** with HTTP trigger for cloud deployment
-   Include **Microsoft Dynamics Business Central** extension example

Files produced are compatible with the original command-line tool (bsc.exe).

## What is BSC?

Block Sorting Compression (**BSC**) is a powerful alternative to dictionary-based methods. It often achieves significantly better compression than **Gzip**, **Zstandard**, or even **7-Zip** on certain data types — especially structured, repetitive content.

Compared to similar high-ratio compressors like **Bzip2** or **Brotli**, BSC delivers **superior compression ratios** while maintaining **faster speeds**. Ilya Grebnov's implementation is highly optimized, supporting multithreading and even CUDA acceleration for GPU boosts.

It excels on:

-   CSV, flat files, Excel exports, JSON, XML
-   Database dumps and backups
-   Binary tabular data (row- or column-oriented)

It performs less effectively on:

-   Already-encoded media (JPG, PNG, GIF, MP3, etc.)
-   Very small files (< 100 KB)
-   Useless on pre-compressed data

### Performance Highlights

Expect **30–50% additional size reduction** than Gzip or Zstandard on suitable data, at the cost of roughly **1.5–3× processing time** on the fastest level. Multithreading can dramatically reduce times on larger files.

I've seen **over 98% reduction** on highly structured numerical datasets at level 3 🔥

**Note:** Running via Azure Functions add the http transfer overher, in Business Central OnPrem also add inerop overhead and does not support multithread, reducing raw speed. Default Azure Function plan offer only 1 core.

### Benchmarks

-   **Gzip**: level 6/9 (optimal)
-   **Zstandard**: level 3/19 (very-fast)
-   **BSC**: level 1/3 (fast)

Hardware: Intel i7-12700KF, 32 GB DDR5, single-threaded no GPU acceleration.

| File | Size | GZ | % | Time | ZStd | % (cumulative) | Time | BSC | % (cumulative) | Time** |
|--|--|--|--|--|--|--|--|--|--|--|
| Json Exemple | 500 KB | 37 KB | -93% | 7/1 | 50 KB | no gain | 30/10 | **20 KB** | **-46%** | 16/0 - (44/16) |
| CSV 100K Records | 16.5 MB | 7.9 MB | -52% | 780/71 | 7.1 MB | -10% | 110/40 | **4.2 MB** | **-41%** | 219/125 - (1'415/548) |
| CSV 1M Records | 50.0 MB | 24.9 MB | -50% | 2960/211 | 23.1 MB | -7% | 310/70 |**12.7 MB** | **-45%** | 640/344 - (4'199/1'656) |
| Binary MySQL Employees exemple | 290 MB | 86.0 MB | -70% | 10'148/952 | 86.5 MB | no gain | 1040/260 |**55.9 MB** | **-35%** | 3'281/2'175 - (18'004/8'062) |


*Times in milliseconds (Compression / Decompression) 
**Direct .NET run – (Business Central interop overhead)

For broader comparisons with top-tier compressors, see line 33in the Large Text Compression Benchmark: [https://www.mattmahoney.net/dc/text.html](https://www.mattmahoney.net/dc/text.html?referrer=grok.com)

----------

# Usage

## .NET Integration

Add a project reference to **bscwrapperCLR .NET 8** or **bscwrapperCLR .NET Framework**.



**CompressOmp** Compresses a data stream.

Parameters:

-   inputStream: Source data
-   outputStream: Destination (includes headers)
-   blockSize: Max block size in bytes (larger → better ratio, more RAM). Default: 25 MB. RAM consumption ~5x block size.
-   numThreads: Threads for multi-block files (0 = auto)
-   lzpHashSize: Hash table size for LZP (0 = disabled, 10–28)
-   lzpMinLen: Min match length for LZP (0 = disabled, 4–255)
-   blockSorter: Sorting algorithm (ST3..ST8 or BWT)
-   coder: Entropy coder (1–3)

Returns: 0 on success or negative error code.

**DecompressOmp** Decompresses a BSC stream.

Parameters:

-   inputStream: Compressed data
-   outputStream: Decompressed result
-   numThreads: Threads (0 = auto)

Returns: 0 on success or negative error code.

## Azure Function

### Deployment Steps

1.  Create an Azure Function App (Windows OS, .NET 8 runtime).
2.  Use the **LibbscSharp AZ Function** project from the repo.
3.  Create a publish profile in Visual Studio and deploy.
4.  Get the base URL from Azure portal > Function App > Overview.
5.  Create an API key (Functions > App keys).

**Auth:** Always include header "x-functions-key: your-key" in API calls.

> **Note (Dec 2025):** Default flexible plans run on Linux. Choose a **Consumption** plan for Windows support.

> **Dependency fix:** If rebuilding the wrapper, manually copy compiled DLLs to the lib folder and ensure they're set to "Copy Always" in the .csproj.

### API Compression Handling

Both endpoints support gzip payloads (Content-Encoding: gzip for input, Accept-Encoding: gzip for output). This reduces transfer size but adds ~10–15% processing time — use only when network bandwidth is the bottleneck.

### Endpoints

-   **Ping** → GET /api/PING (test connectivity)
-   **Compress** → POST /api/COMPRESS?coder=1&blocksize=25
    -   coder (required): 1 (fast), 2 (medium), 3 (high)
    -   blocksize (optional): In MB. Default: 25
    -   Body: Raw data (optionally gzipped)
    -   Response: BSC-compressed data
-   **Decompress** → POST /api/DECOMPRESS
    -   Body: BSC-compressed data
    -   Response: Decompressed data (optionally gzipped if requested)

## Business Central (Cloud)

Deploy the Azure Function as above, then call from AL:


    var 
        BSCCompression: Codeunit "BSC Data Compression"; 
    begin 
        // Compress 
        BSCCompression.AzureLibbscCompress(URL, Key, InputStr, CompressedStr, 1);
        BSCCompression.AzureLibbscDecompress(URL, Key, CompressedStr, OutputStr);

The repo includes a full extension example.

## Business Central (On-Premise)

1.  Copy required DLLs:
    -   BC V23+: To extension .netpackages folder + server Add-ins folder
        -   bscwrapperCLR .NET 8.dll
        -   Ijwhost.dll
        -   LibbscSharp Net Core.dll
    -   Older versions: Use .NET Framework equivalents
2.  Restart BC service and VS Code.
3.  Declare assembly in app.json:

JSON

```
dotnet {
  assembly("LibbscSharp Net Core") {
    type("LibbscSharp.LibscSharp", "LibbscSharp") { }
  }
}
```

4.  Use directly or via helper codeunit:
```
var 
    Libbsc: DotNet LibbscSharp; 
begin 
    Libbsc := Libbsc.LibscSharp(); // Compress (example params) 
    Libbsc.BSCCompress(InputStream, OutputStream, 0, 0, 100 * 1024 * 1024); // Decompress
    Libbsc.BSCDecompress(InputStream, OutputStream, 0); end;
```

Or with the helper codeunit:

```
var 
    BSCCompression: Codeunit "BSC Data Compression"; 
begin 
    BSCCompression.Compress(InputStr, CompressedStr, 1); 
    BSCCompression.Decompress(CompressedStr, OutputStr);
```

## Error Codes

**From libbsc:**

-   -1: LIBBSC_BAD_PARAMETER
-   -2: LIBBSC_NOT_ENOUGH_MEMORY
-   -3: LIBBSC_NOT_COMPRESSIBLE
-   -4: LIBBSC_NOT_SUPPORTED
-   -5: LIBBSC_UNEXPECTED_EOB
-   -6: LIBBSC_DATA_CORRUPT
-   -7: LIBBSC_GPU_ERROR
-   -8: LIBBSC_GPU_NOT_SUPPORTED
-   -9: LIBBSC_GPU_NOT_ENOUGH_MEMORY

**From CLR wrapper:**

-   -20: LIBBSC_COMPLVL_OUTRANGE
-   -21: LIBBSC_BAD_PARAM
-   -23: LIBBSC_NOT_SEEKABLE
