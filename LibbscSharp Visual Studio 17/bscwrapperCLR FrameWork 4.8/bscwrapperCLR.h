using namespace System;
using namespace System::IO;

#pragma once
namespace BscDotNet {
    public ref class Compressor {
    public:
        // OMP
        static int CompressOmp(Stream^ inputStream, Stream^ outputStream, int blockSize, int NumThreads, int lzpHashSize, int lzpMinLen, int blockSorter/*1=LIBBSC_BLOCKSORTER_BWT*/, int coder/*1=LIBBSC_CODER_QLFC_STATIC*/);
        static int DecompressOmp(Stream^ inputStream, Stream^ outputStream, int numThreads);

        // Compress: inputStream -> outputStream
        static int Compress(Stream^ inputStream, Stream^ outputStream, int compressionLevel);
        
        // Decompress: inputStream -> outputStream
        static int Decompress(Stream^ inputStream, Stream^ outputStream);
    };
}