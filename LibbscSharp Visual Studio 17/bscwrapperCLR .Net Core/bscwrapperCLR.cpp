#include "pch.h"
#include "framework.h"
#include "bscwrapperCLR.h"
#include "libbsc.h"
#include "filters.h"
#include "omp.h"
#include "iostream"
#include "msclr/marshal.h"


#define LIBBSC_CONTEXTS_FOLLOWING    1
#define LIBBSC_CONTEXTS_PRECEDING    2
#define LIBBSC_CONTEXTS_AUTODETECT   3
#define LIBBSC_BLOCK_TYPE_BSC        1
#define LIBBSC_COMPLVL_OUTRANGE      -20
#define LIBBSC_BAD_PARAM             -21
#define LIBBSC_NOT_SEEKABLE          -23

using namespace System;
using namespace System::IO;
using namespace System::IO::MemoryMappedFiles;
using namespace System::Runtime::InteropServices;
using namespace System::Security::Permissions;

typedef struct BSC_BLOCK_HEADER
{
    long long       blockOffset;
    signed char     recordSize;
    signed char     sortingContexts;
} BSC_BLOCK_HEADER;


/**
* Compress a stream of data.
* @param inputStream                        - the input data to compress
* @param outputStream                       - the output compressed data including global header + blocks headers
* @param blockSize                          - the maximum block size in Byte to compress sequentially, higher value improve ratio while consuming more RAM. Default if 25 MB.
* @param NumThreads                         - the number of threads to use if the file is multy-blocks (depend on input size and block size)
* @param lzpHashSize                        - the hash table size if LZP enabled, 0 otherwise. Must be in range [0, 10..28].
* @param lzpMinLen                          - the minimum match length if LZP enabled, 0 otherwise. Must be in range [0, 4..255].
* @param blockSorter                        - the block sorting algorithm. Must be in range [ST3..ST8, BWT].
* @param coder                              - the entropy coding algorithm. Must be in range 1..3
* @return 0 if succed, nagative value for error code
*/
int BscDotNet::Compressor::CompressOmp(Stream^ inputStream, Stream^ outputStream, int blockSize, int NumThreads, int lzpHashSize, int lzpMinLen, int blockSorter, int coder)
{
    // Checks
    if (inputStream == nullptr || outputStream == nullptr) return LIBBSC_BAD_PARAM;
    if (!inputStream->CanRead || !outputStream->CanWrite || !inputStream->CanSeek) return LIBBSC_NOT_SEEKABLE;
    if (coder < 1 || coder > 3) return LIBBSC_COMPLVL_OUTRANGE;

    // Default values
    if (lzpHashSize == 0) lzpHashSize = 16;
    if (lzpMinLen == 0) lzpMinLen = 128;
    int features = LIBBSC_DEFAULT_FEATURES;
    signed char sortingContexts = LIBBSC_CONTEXTS_FOLLOWING; // or bsc auto detect
    if (NumThreads > 0)
        omp_set_num_threads(NumThreads);
    bsc_init(features);
    
    // Calc block number :
    long long fileSize = inputStream->Length;
    int nBlocks = (blockSize > 0) ? (int)((fileSize + (long long)blockSize - 1) / blockSize) : 0;
    
    signed char recordSize = 1;
    long long blockOffset = 0, position = 0;
    int compressionError = LIBBSC_NO_ERROR;

    // === Global BSC1 header ===
    outputStream->Write(gcnew array<Byte>{ 'b', 's', 'c', 0x31 }, 0, 4);  // "bsc1"
    array<Byte>^ nBlocksBytes = gcnew array<Byte>(4);
    pin_ptr<Byte> pinN = &nBlocksBytes[0];
    *(int*)pinN = nBlocks;  // little-endian int
    outputStream->Write(nBlocksBytes, 0, 4);   
    
    // Loop until all stream proceed
    inputStream->Position = 0;
#pragma omp parallel for schedule(dynamic) if(nBlocks > 1)
    for (int blockIndex = 0; blockIndex < nBlocks; blockIndex++)
    {
        long long blockOffset = (long long)blockIndex * blockSize;
        int currentBlockSize = (int)System::Math::Min((long long)blockSize, fileSize - blockOffset);

        // Allouer buffers par thread
        array<unsigned char>^ inputBlock = gcnew array<unsigned char>(currentBlockSize);
        array<unsigned char>^ outputBlock = gcnew array<unsigned char>(currentBlockSize + LIBBSC_HEADER_SIZE + 2048);

        // Read
#pragma omp critical(input_stream)
        {
            inputStream->Position = blockOffset;
            inputStream->Read(inputBlock, 0, currentBlockSize);
        }

        pin_ptr<unsigned char> pinInput = &inputBlock[0];
        pin_ptr<unsigned char> pinOutput = &outputBlock[0];
        unsigned char* buffer = pinInput;
        unsigned char* outBuffer = pinOutput;

        unsigned char* compressPtr = outBuffer + 10;  // leave 10-byte gap for block header
        // Compress
        int compressedSize = bsc_compress(buffer, compressPtr, currentBlockSize + 10, lzpHashSize, lzpMinLen, blockSorter, coder, features);
        if (compressedSize < 0)
        {
            if (compressionError == LIBBSC_NO_ERROR)
                compressionError = compressedSize;
            break;
        }

        // Now write header at start
        unsigned char* headerPtr = outBuffer;
        uint64_t offset64 = blockOffset;  // assuming blockOffset is uint64_t
        std::memcpy(headerPtr + 0, &offset64, 8);
        headerPtr[8] = (unsigned char)recordSize;
        headerPtr[9] = (unsigned char)sortingContexts;

        // Then write the whole thing in one go
#pragma omp critical(output_stream)
        {
            // If outputStream is System::IO::Stream^ (managed)
            // Pin the native buffer and write
            pin_ptr<unsigned char> pinned = &outBuffer[0];
            array<Byte>^ managedWrapper = gcnew array<Byte>(10 + compressedSize);
            Marshal::Copy(IntPtr(pinned), managedWrapper, 0, 10 + compressedSize);
            outputStream->Write(managedWrapper, 0, 10 + compressedSize);
        }
    }
    if (compressionError != LIBBSC_NO_ERROR)
        return compressionError;
    return LIBBSC_NO_ERROR;
}

/**
* Decompress a stream of data.
* @param inputStream                        - the compressed input data
* @param outputStream                       - the output decompressed data result
* @param numThreads                         - the number of threads to use if the file is multy-blocks
* @return 0 if succed, nagative value for error code
*/
int BscDotNet::Compressor::DecompressOmp(Stream^ inputStream, Stream^ outputStream, int numThreads)
{
    if (inputStream == nullptr || outputStream == nullptr)
        return LIBBSC_BAD_PARAM;
    if (!inputStream->CanRead || !inputStream->CanSeek)
        return LIBBSC_NOT_SEEKABLE;

    bsc_init(LIBBSC_DEFAULT_FEATURES);
    int features = LIBBSC_DEFAULT_FEATURES;

#ifdef _OPENMP
    if (numThreads > 0)
        omp_set_num_threads(numThreads);
#endif

    long long fileSize = inputStream->Length;
    inputStream->Position = 0;

    // === Lire header global ===
    array<Byte>^ sign = gcnew array<Byte>(4);
    int read = inputStream->Read(sign, 0, 4);
    if (read < 4 || sign[0] != 'b' || sign[1] != 's' || sign[2] != 'c' || sign[3] != 0x31)
        return LIBBSC_NOT_SUPPORTED; // Pas un fichier BSC valide

    array<Byte>^ nBlocksBytes = gcnew array<Byte>(4);
    read = inputStream->Read(nBlocksBytes, 0, 4);
    if (read < 4) return LIBBSC_DATA_CORRUPT;
    pin_ptr<Byte> pinN = &nBlocksBytes[0];
    int nBlocks = *(int*)pinN;

    if (nBlocks <= 0) return LIBBSC_DATA_CORRUPT;

    int decompressionError = LIBBSC_NO_ERROR;

#pragma omp parallel if(nBlocks > 1) shared(decompressionError)
    {
        // Buffers par thread (réutilisés)
        array<unsigned char>^ threadBuffer = nullptr;
        int bufferCapacity = 0;

#pragma omp for schedule(dynamic)
        for (int blockIndex = 0; blockIndex < nBlocks; blockIndex++)
        {
            signed char recordSize = 0;
            signed char sortingContexts = 0;
            long long blockOffset = 0;
            int blockSize = 0;
            int dataSize = 0;

            array<Byte>^ blockHeaderBytes = gcnew array<Byte>(10);
            array<unsigned char>^ compressedBlock = nullptr;

            // === Lecture du block header (10 octets) + bloc compressé ===
#pragma omp critical(input_stream)
            {
                if (inputStream->Position >= fileSize)
                {
                    decompressionError = LIBBSC_DATA_CORRUPT;
                }

                inputStream->Read(blockHeaderBytes, 0, 10);
                pin_ptr<Byte> pinH = &blockHeaderBytes[0];
                BSC_BLOCK_HEADER* header = (BSC_BLOCK_HEADER*)pinH;
                blockOffset = header->blockOffset;
                recordSize = header->recordSize;
                sortingContexts = header->sortingContexts;

                if (recordSize < 1 ||
                    (sortingContexts != LIBBSC_CONTEXTS_FOLLOWING && sortingContexts != LIBBSC_CONTEXTS_PRECEDING))
                {
                    decompressionError = LIBBSC_NOT_SUPPORTED;
                }

                // Lire le header interne libbsc (28 octets) pour connaître les tailles
                array<unsigned char>^ bscHeader = gcnew array<unsigned char>(LIBBSC_HEADER_SIZE);
                inputStream->Read(bscHeader, 0, LIBBSC_HEADER_SIZE);
                pin_ptr<unsigned char> pinBsc = &bscHeader[0];

                int rc = bsc_block_info(pinBsc, LIBBSC_HEADER_SIZE, &blockSize, &dataSize, features);
                if (rc != LIBBSC_NO_ERROR)
                {
                    decompressionError = rc;
                }

                // Lire le reste du bloc compressé
                int payloadSize = blockSize - LIBBSC_HEADER_SIZE;
                compressedBlock = gcnew array<unsigned char>(blockSize);
                Array::Copy(bscHeader, 0, compressedBlock, 0, LIBBSC_HEADER_SIZE);
                inputStream->Read(compressedBlock, LIBBSC_HEADER_SIZE, payloadSize);
            }

            // Allouer/réutiliser buffer décompression
            int needed = System::Math::Max(blockSize, dataSize);
            if (threadBuffer == nullptr || threadBuffer->Length < needed)
            {
                threadBuffer = gcnew array<unsigned char>(needed + 2048);
            }

            pin_ptr<unsigned char> pinBuf = &threadBuffer[0];
            unsigned char* buffer = pinBuf;

            // Copier le bloc compressé
            pin_ptr<unsigned char> pinSrc = &compressedBlock[0];
            pin_ptr<unsigned char> pinDst = &threadBuffer[0];
            memcpy(pinDst, pinSrc, blockSize);

            // === Décompression ===
            int result = bsc_decompress(buffer, blockSize, buffer, dataSize, features);
            if (result < LIBBSC_NO_ERROR)
            {
#pragma omp critical(error_handling)
                {
                    if (decompressionError == LIBBSC_NO_ERROR)
                        decompressionError = result;
                }
            }

            // Reverse si PRECEDING
            if (sortingContexts == LIBBSC_CONTEXTS_PRECEDING)
            {
                result = bsc_reverse_block(buffer, dataSize, features);
                if (result != LIBBSC_NO_ERROR)
                {
                    decompressionError = result;
                }
            }

            // Reorder reverse si recordSize > 1
            if (recordSize > 1)
            {
                result = bsc_reorder_reverse(buffer, dataSize, recordSize, features);
                if (result != LIBBSC_NO_ERROR)
                {
                    decompressionError = result;
                }
            }

            // === Écriture dans le bon ordre (seek + write) ===
#pragma omp critical(output_stream)
            {
                if (decompressionError == LIBBSC_NO_ERROR)
                {
                    outputStream->Position = blockOffset;
                    array<Byte>^ outputData = gcnew array<Byte>(dataSize);
                    Marshal::Copy(IntPtr(buffer), outputData, 0, dataSize);
                    outputStream->Write(outputData, 0, dataSize);
                }
            }
        }
    }

    if (decompressionError != LIBBSC_NO_ERROR)
        return decompressionError;
    return LIBBSC_NO_ERROR;
}