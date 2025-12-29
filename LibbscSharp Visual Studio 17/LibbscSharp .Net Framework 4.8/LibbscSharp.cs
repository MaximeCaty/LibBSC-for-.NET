using BscDotNet;
using System.IO;
using System.Runtime;

namespace LibbscSharp
{
    static public class LibscSharp
    {
        static public int BSCCompress(Stream inputStream, Stream outputStream, int CompressionLevel)
        {
            int result = Compressor.CompressOmp(inputStream, outputStream, 10 * 1024 * 1024, 0, 0, 0, 1, CompressionLevel);
            return result;
        }

        static public int BSCDecompress(Stream inputStream, Stream outputStream)
        {
            int DecompressCode = Compressor.DecompressOmp(inputStream, outputStream, 0);
            return DecompressCode;
        }

        static public int BSCCompress(Stream inputStream, Stream outputStream, int CompressionLevel, int Numthreads)
        {
            int result = Compressor.CompressOmp(inputStream, outputStream, 10 * 1024 * 1024, Numthreads, 0, 0, 1, CompressionLevel);
            return result;
        }

        static public int BSCCompress(Stream inputStream, Stream outputStream, int CompressionLevel, int Numthreads, int BlobkSize)
        {
            int result = Compressor.CompressOmp(inputStream, outputStream, BlobkSize, Numthreads, 0, 0, 1, CompressionLevel);
            return result;
        }

        static public int BSCDecompress(Stream inputStream, Stream outputStream, int Numthreads)
        {
            int DecompressCode = Compressor.DecompressOmp(inputStream, outputStream, Numthreads);
            return DecompressCode;
        }
    }
}
