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
Compress a stream of data.
@param inputData                   - the input data to compress
@param dataLength                  - input data exact length
@param outputStream                - the output compressed data including global header + blocks headers
@param blockSize                   - the maximum block size in Byte to compress sequentially, higher value improve ratio while consuming more RAM. Default if 25 MB.
@param NumThreads                  - the number of threads to use if the file is multy-blocks (depend on input size and block size)
@param lzpHashSize                 - the hash table size if LZP enabled, 0 otherwise. Must be in range [0, 10..28].
@param lzpMinLen                   - the minimum match length if LZP enabled, 0 otherwise. Must be in range [0, 4..255].
@param blockSorter                 - the block sorting algorithm. Must be in range [ST3..ST8, BWT].
@param coder                       - the entropy coding algorithm. Must be in range 1..3
@return 0 if succed, nagative value for error code
*/
int BscDotNet::Compressor::CompressOmp(
    array<unsigned char>^ inputData,
    long long dataLength,
    Stream^ outputStream,
    int blockSize,
    int NumThreads,
    int lzpHashSize,
    int lzpMinLen,
    int blockSorter,
    int coder)
{
    if (inputData == nullptr || outputStream == nullptr) return LIBBSC_BAD_PARAM;
    if (!outputStream->CanWrite) return LIBBSC_BAD_PARAM;
    if (coder < 1 || coder > 3) return LIBBSC_COMPLVL_OUTRANGE;

    if (lzpHashSize == 0) lzpHashSize = 16;
    if (lzpMinLen == 0) lzpMinLen = 128;

    int features = LIBBSC_DEFAULT_FEATURES;
    signed char sortingContexts = LIBBSC_CONTEXTS_FOLLOWING;

    if (NumThreads > 0)
        omp_set_num_threads(NumThreads);

    bsc_init(features);

    // Calc block number :
    long long fileSize = dataLength; // inputData->Length;
    if (fileSize == 0) {
        return LIBBSC_BAD_PARAM;
    }
    int nBlocks = (blockSize > 0) ? (int)((fileSize + (long long)blockSize - 1) / blockSize) : 0;

    // Write global header
    outputStream->Write(gcnew array<Byte>{ 'b', 's', 'c', 0x31 }, 0, 4);
    array<Byte>^ nBlocksBytes = gcnew array<Byte>(4);
    pin_ptr<Byte> pinN = &nBlocksBytes[0];
    *(int*)pinN = nBlocks;
    outputStream->Write(nBlocksBytes, 0, 4);

    signed char recordSize = 1;
    int compressionError = LIBBSC_NO_ERROR;

    // Pre-allocate one temporary working buffer per thread (max block size + header + safety)
    const int headerSize = 10;
    const int extraSafety = 2048;
    array<unsigned char>^ tempBuffer = gcnew array<unsigned char>(blockSize + headerSize + extraSafety);

    pin_ptr<unsigned char> pinTemp = &tempBuffer[0];
    unsigned char* workBuffer = pinTemp;

#pragma omp parallel for schedule(dynamic) if(nBlocks > 1)
    for (int blockIndex = 0; blockIndex < nBlocks; blockIndex++)
    {
        if (compressionError != LIBBSC_NO_ERROR) continue;

        long long blockOffset = (long long)blockIndex * blockSize;
        int currentBlockSize = (int)System::Math::Min((long long)blockSize, fileSize - blockOffset);

        // Pin the input block directly from inputData
        pin_ptr<unsigned char> pinInput = &inputData[(int)blockOffset];
        unsigned char* inputPtr = pinInput;

        // Copy input block to temp buffer + header space
        // This is the ONLY copy we do per block
        std::memcpy(workBuffer + headerSize, inputPtr, currentBlockSize);

        // In-place compression: input and output are the same (after header)
        unsigned char* compressPtr = workBuffer + headerSize;

        int compressedSize = bsc_compress(
            compressPtr,           // input
            compressPtr,           // output == input → triggers in-place path
            currentBlockSize,
            lzpHashSize,
            lzpMinLen,
            blockSorter,
            coder,
            features);

        if (compressedSize < 0)
        {
#pragma omp critical(error)
            {
                if (compressionError == LIBBSC_NO_ERROR)
                    compressionError = compressedSize;
            }
        continue;
        }

        // Write block header
        uint64_t offset64 = (uint64_t)blockOffset;
        std::memcpy(workBuffer + 0, &offset64, 8);
        workBuffer[8] = (unsigned char)recordSize;
        workBuffer[9] = (unsigned char)sortingContexts;

        // Write header + compressed data in one go
#pragma omp critical(output_stream)
        {
            pin_ptr<unsigned char> pinWrite = workBuffer;
            array<Byte>^ writeArray = tempBuffer; // reuse the same array
            outputStream->Write(writeArray, 0, headerSize + compressedSize);
        }
    }

    return compressionError != LIBBSC_NO_ERROR ? compressionError : LIBBSC_NO_ERROR;
}

/**
* Compress single block of data without writting header
* @param inputStream                        - the input data to compress
* @param outputStream                       - the output compressed data including global header + blocks headers
* @param lzpHashSize                        - the hash table size if LZP enabled, 0 otherwise. Must be in range [0, 10..28].
* @param lzpMinLen                          - the minimum match length if LZP enabled, 0 otherwise. Must be in range [0, 4..255].
* @param blockSorter                        - the block sorting algorithm. Must be in range [ST3..ST8, BWT].
* @param coder                              - the entropy coding algorithm. Must be in range 1..3
* @return 0 if succed, nagative value for error code
*/
/*
int BscDotNet::Compressor::CompressSingleBlock(
    Stream^ inputStream,
    Stream^ outputStream,
    int lzpHashSize,
    int lzpMinLen,
    int blockSorter,
    int coder)
{
    // Parameter validation
    if (inputStream == nullptr || outputStream == nullptr) return LIBBSC_BAD_PARAM;
    if (!inputStream->CanRead || !outputStream->CanWrite) return LIBBSC_BAD_PARAM;
    if (!inputStream->CanSeek) return LIBBSC_NOT_SEEKABLE;
    if (coder < 1 || coder > 3) return LIBBSC_COMPLVL_OUTRANGE;

    // Default parameter handling
    bsc_init(LIBBSC_DEFAULT_FEATURES);
    if (lzpHashSize == 0) lzpHashSize = 16;
    if (lzpMinLen == 0) lzpMinLen = 128;
    int features = LIBBSC_DEFAULT_FEATURES;

    // Get input size
    long long inputLength = inputStream->Length;
    if (inputLength > INT_MAX)
        return LIBBSC_BAD_PARAMETER; // libbsc uses int for sizes

    int inputSize = static_cast<int>(inputLength);
    if (inputSize == 0)
    {
        // Empty input
        return LIBBSC_NOT_SEEKABLE;
    }

    // Allocate a single buffer large enough for in-place compression
    // libbsc needs some overhead (~few KB), so add safety margin
    const int overhead = 2048;
    int bufferSize = inputSize + overhead;

    array<unsigned char>^ buffer = gcnew array<unsigned char>(bufferSize);

    // Read entire input into buffer
    int bytesRead = 0;
    while (bytesRead < inputSize)
    {
        int read = inputStream->Read(buffer, bytesRead, inputSize - bytesRead);
        if (read <= 0) break; // EOF or error
        bytesRead += read;
    }

    if (bytesRead != inputSize)
        return LIBBSC_DATA_CORRUPT;

    // Pin the buffer for native access
    pin_ptr<unsigned char> pinnedBuffer = &buffer[0];
    unsigned char* dataPtr = pinnedBuffer;

    // Perform in-place compression: output written into same buffer
    int compressedSize = bsc_compress(
        dataPtr,           // input
        dataPtr,           // output (same buffer → in-place avoid double ram allocation)
        inputSize,
        lzpHashSize,
        lzpMinLen,
        blockSorter,
        coder,
        features);

    if (compressedSize < 0)
        return compressedSize; // Propagate libbsc error

    if (compressedSize > bufferSize)
        return LIBBSC_NOT_ENOUGH_MEMORY; // Should never happen with our overhead

    // Write only the compressed block data (no header)
    outputStream->Write(buffer, 0, compressedSize);
    return 0; // Success
}*/

/**
* Decompress a stream of data.
* @param inputData                          - the compressed input data
* @param dataLength                         - input data exact length
* @param outputStream                       - the output decompressed data result
* @param numThreads                         - the number of threads to use if the file is multy-blocks
* @return 0 if succed, nagative value for error code
*/
int BscDotNet::Compressor::DecompressOmp(
    array<unsigned char>^ inputData,
    long long dataLength,
    Stream^ outputStream,
    int numThreads)
{
    if (inputData == nullptr || outputStream == nullptr || dataLength <= 0 || dataLength > inputData->Length)
        return LIBBSC_BAD_PARAM;
    if (!outputStream->CanWrite || !outputStream->CanSeek) return LIBBSC_BAD_PARAM;

    bsc_init(LIBBSC_DEFAULT_FEATURES);
    int features = LIBBSC_DEFAULT_FEATURES;

#ifdef _OPENMP
    if (numThreads > 0) omp_set_num_threads(numThreads);
#endif

    pin_ptr<unsigned char> pinInput = &inputData[0];
    unsigned char* inputPtr = pinInput;

    long long pos = 0;

    // Global header check (single-threaded, safe)
    if (dataLength < 8 ||
        inputPtr[0] != 'b' || inputPtr[1] != 's' || inputPtr[2] != 'c' || inputPtr[3] != 0x31)
        return LIBBSC_NOT_SUPPORTED;

    int nBlocks = *(int*)(inputPtr + 4);
    if (nBlocks <= 0) return LIBBSC_DATA_CORRUPT;

    pos = 8;

    int decompressionError = LIBBSC_NO_ERROR;

#pragma omp parallel if(nBlocks > 1) shared(decompressionError, pos)
    {
        // Per-thread buffer (start with reasonable size, resize if needed)
        array<unsigned char>^ threadBuffer = gcnew array<unsigned char>(32 * 1024 * 1024 + 4096);
        pin_ptr<unsigned char> pinBuf = &threadBuffer[0];
        unsigned char* buffer = pinBuf;
        int bufferCapacity = threadBuffer->Length;

        long long localHeaderPos = 0;
        int localBlockSize = 0;  // total compressed size including LIBBSC_HEADER_SIZE

#pragma omp for schedule(dynamic)
        for (int blockIndex = 0; blockIndex < nBlocks; blockIndex++)
        {
            if (decompressionError != LIBBSC_NO_ERROR) continue;

            long long blockOffset = 0;
            signed char recordSize = 0;
            signed char sortingContexts = 0;
            int dataSize = 0;

            bool blockValid = false;

            // === Fully atomic block header + size reading ===
#pragma omp critical(input_reading)
            {
                if (pos + 10 > dataLength)
                {
                    decompressionError = LIBBSC_DATA_CORRUPT;
                }
                else
                {
                    unsigned char* headerPtr = inputPtr + pos;

                    blockOffset = *(uint64_t*)headerPtr;
                    recordSize = headerPtr[8];
                    sortingContexts = headerPtr[9];

                    if (recordSize < 1 ||
                        (sortingContexts != LIBBSC_CONTEXTS_FOLLOWING && sortingContexts != LIBBSC_CONTEXTS_PRECEDING))
                    {
                        decompressionError = LIBBSC_NOT_SUPPORTED;
                    }
                    else if (pos + 10 + LIBBSC_HEADER_SIZE > dataLength)
                    {
                        decompressionError = LIBBSC_DATA_CORRUPT;
                    }
                    else
                    {
                        int rc = bsc_block_info(inputPtr + pos + 10, LIBBSC_HEADER_SIZE, &localBlockSize, &dataSize, features);
                        if (rc != LIBBSC_NO_ERROR)
                        {
                            decompressionError = rc;
                        }
                        else
                        {
                            if (pos + 10 + localBlockSize <= dataLength)
                            {
                                localHeaderPos = pos;
                                pos += 10 + localBlockSize;  // advance only if fully valid
                                blockValid = true;
                            }
                        }
                    }
                }
            }

            if (!blockValid || decompressionError != LIBBSC_NO_ERROR) continue;

            // === Safe copy into thread buffer ===
            int needed = System::Math::Max(localBlockSize, dataSize);
            if (needed > bufferCapacity)
            {
                threadBuffer = gcnew array<unsigned char>(needed + 4096);
                pinBuf = &threadBuffer[0];
                buffer = pinBuf;
                bufferCapacity = threadBuffer->Length;
            }

            // This memcpy is now guaranteed safe
            std::memcpy(buffer, inputPtr + localHeaderPos + 10, localBlockSize);

            // === In-place decompression ===
            int result = bsc_decompress(buffer, localBlockSize, buffer, dataSize, features);
            if (result < LIBBSC_NO_ERROR)
            {
#pragma omp critical(error_handling)
                if (decompressionError == LIBBSC_NO_ERROR)
                    decompressionError = result;
                continue;
            }

            if (sortingContexts == LIBBSC_CONTEXTS_PRECEDING)
            {
                result = bsc_reverse_block(buffer, dataSize, features);
                if (result != LIBBSC_NO_ERROR)
                {
#pragma omp critical(error_handling)
                    decompressionError = result;
                }
                continue;
            }

            if (recordSize > 1)
            {
                result = bsc_reorder_reverse(buffer, dataSize, recordSize, features);
                if (result != LIBBSC_NO_ERROR)
                {
#pragma omp critical(error_handling)
                    decompressionError = result;
                }
                continue;
            }

            // === Write output ===
#pragma omp critical(output_stream)
            {
                if (decompressionError == LIBBSC_NO_ERROR)
                {
                    outputStream->Position = blockOffset;
                    outputStream->Write(threadBuffer, 0, dataSize);
                }
            }
        }
    }

    return decompressionError != LIBBSC_NO_ERROR ? decompressionError : LIBBSC_NO_ERROR;
}

/*
int BscDotNet::Compressor::DecompressSingleBlock(Stream^ inputStream, Stream^ outputStream)
{
    if (inputStream == nullptr || outputStream == nullptr) return LIBBSC_BAD_PARAM;
    if (!inputStream->CanRead || !inputStream->CanSeek) return LIBBSC_NOT_SEEKABLE;

    const int EXTERNAL_HEADER_SIZE = 10;
    const int INTERNAL_HEADER_SIZE = LIBBSC_HEADER_SIZE; // 28

    // Read the internal 28-byte libbsc header
    array<unsigned char>^ intHeader = gcnew array<unsigned char>(INTERNAL_HEADER_SIZE);
    int read = inputStream->Read(intHeader, 0, INTERNAL_HEADER_SIZE);
    if (read < INTERNAL_HEADER_SIZE)
        return LIBBSC_DATA_CORRUPT;

    pin_ptr<unsigned char> pinInt = &intHeader[0];
    int blockSize = 0, dataSize = 0;
    int rc = bsc_block_info(pinInt, INTERNAL_HEADER_SIZE, &blockSize, &dataSize, LIBBSC_DEFAULT_FEATURES);
    if (rc != LIBBSC_NO_ERROR)
        return rc;

    if (dataSize <= 0 || blockSize <= INTERNAL_HEADER_SIZE)
        return LIBBSC_DATA_CORRUPT;

    int payloadSize = blockSize - INTERNAL_HEADER_SIZE;

    // Allocate single buffer large enough for compressed block OR decompressed data (whichever is larger)
    // Add small safety margin
    int maxNeeded = System::Math::Max(blockSize, dataSize) + 2048;
    array<unsigned char>^ buffer = gcnew array<unsigned char>(maxNeeded);

    pin_ptr<unsigned char> pinBuffer = &buffer[0];
    unsigned char* buf = pinBuffer;

    // Copy internal header into buffer
    Marshal::Copy(intHeader, 0, IntPtr(buf), INTERNAL_HEADER_SIZE);

    // Read compressed payload directly into buffer after header
    read = 0;
    while (read < payloadSize)
    {
        int chunk = inputStream->Read(buffer, INTERNAL_HEADER_SIZE + read, payloadSize - read);
        if (chunk <= 0) return LIBBSC_DATA_CORRUPT;
        read += chunk;
    }

    // In-place decompression: decompress into same buffer (safe because dataSize <= maxNeeded)
    rc = bsc_decompress(buf, blockSize, buf, dataSize, LIBBSC_DEFAULT_FEATURES);
    if (rc < LIBBSC_NO_ERROR)
        return rc;

    // Write decompressed data
    outputStream->Write(buffer, 0, dataSize);

    return LIBBSC_NO_ERROR;
}
*/