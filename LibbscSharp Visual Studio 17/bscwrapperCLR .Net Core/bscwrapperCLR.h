using namespace System;
using namespace System::IO;

#pragma once
namespace BscDotNet {
    public ref class Compressor {
    public:
        // OMP
        static int CompressOmp(array<unsigned char>^ inputData, long long dataLength, Stream^ outputStream, int blockSize, int NumThreads, int lzpHashSize, int lzpMinLen, int blockSorter, int coder);
        static int DecompressOmp(array<unsigned char>^ inputData, long long dataLength, Stream^ outputStream, int numThreads);
        
        // Single block
        //static int CompressSingleBlock(Stream^ inputStream, Stream^ outputStream, int lzpHashSize, int lzpMinLen, int blockSorter, int coder);
        //static int DecompressSingleBlock(Stream^ inputStream, Stream^ outputStream);
    };
}