using BscDotNet;
using System.IO;

namespace LibbscSharp
{
    public class LibscSharp
    {
        public int BSCCompress(Stream inputStream, Stream outputStream, int CompressionLevel)
        {
            int result = Compressor.CompressOmp(inputStream, outputStream, 10 * 1024 * 1024, 0, 0, 0, 1, CompressionLevel);
            return result;
        }

        public int BSCDecompress(Stream inputStream, Stream outputStream)
        {
            int DecompressCode = Compressor.DecompressOmp(inputStream, outputStream, 0);
            return DecompressCode;
        }

        public int BSCCompress(Stream inputStream, Stream outputStream, int CompressionLevel, int Numthreads)
        {
            int result = Compressor.CompressOmp(inputStream, outputStream, 10 * 1024 * 1024, Numthreads, 0, 0, 1, CompressionLevel);
            return result;
        }

        public int BSCCompress(Stream inputStream, Stream outputStream, int CompressionLevel, int Numthreads, int BlobkSize)
        {
            int result = Compressor.CompressOmp(inputStream, outputStream, BlobkSize, Numthreads, 0, 0, 1, CompressionLevel);
            return result;
        }

        public int BSCDecompress(Stream inputStream, Stream outputStream, int Numthreads)
        {
            int DecompressCode = Compressor.DecompressOmp(inputStream, outputStream, Numthreads);
            return DecompressCode;
        }
    }
}
