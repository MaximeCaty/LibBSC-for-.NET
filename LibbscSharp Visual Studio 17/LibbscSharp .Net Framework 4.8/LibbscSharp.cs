using System;
using System.IO;
using System.Runtime;
using BscDotNet;

namespace LibbscSharp
{
    static public class LibscSharp
    {
        static public int BSCCompress(Stream inputStream, Stream outputStream, int CompressionLevel)
        {
            return BSCCompress(inputStream, outputStream, CompressionLevel, 0, 25 * 1024 * 1024);
        }

        static public int BSCCompress(Stream inputStream, Stream outputStream, int CompressionLevel, int Numthreads)
        {
            return BSCCompress(inputStream, outputStream, CompressionLevel, Numthreads, 25 * 1024 * 1024);
        }

        static public int BSCCompress(Stream inputStream, Stream outputStream, int CompressionLevel, int Numthreads, int BlobkSize)
        {
            //return Compressor.CompressOmp(inputStream, outputStream, BlobkSize, 0, 0, 0, 1, CompressionLevel);
            if (inputStream is MemoryStream ms && ms.TryGetBuffer(out ArraySegment<byte> segment))
            {
                // pass underlying stream buffer without copy
                return Compressor.CompressOmp(segment.Array, inputStream.Length, outputStream, BlobkSize, 0, 0, 0, 1, CompressionLevel);
            }
            else
            {
                long length = inputStream.Length;
                byte[] buffer = new byte[length];
                inputStream.Position = 0;
                inputStream.Read(buffer, 0, (int)length);
                return Compressor.CompressOmp(buffer, length, outputStream, BlobkSize, 0, 0, 0, 1, CompressionLevel);
            }
        }
        static public int BSCDecompress(Stream inputStream, Stream outputStream)
        {
            return BSCDecompress(inputStream, outputStream);
        }

        static public int BSCDecompress(Stream inputStream, Stream outputStream, int Numthreads)
        {
            //return Compressor.DecompressOmp(inputStream, outputStream, Numthreads);
            if (inputStream is MemoryStream ms && ms.TryGetBuffer(out ArraySegment<byte> segment))
            {
                // pass underlying stream buffer without copy
                return Compressor.DecompressOmp(segment.Array, inputStream.Length, outputStream, Numthreads);
            }
            else
            {
                long length = inputStream.Length;
                byte[] buffer = new byte[length];
                inputStream.Position = 0;
                inputStream.Read(buffer, 0, (int)length);
                return Compressor.DecompressOmp(buffer, length, outputStream, Numthreads);
            }
        }
    }
}
