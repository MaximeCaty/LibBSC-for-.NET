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
#define LIBBSC_COMPLVL_OUTRANGE     -20
#define LIBBSC_CONTEXTS_AUTODETECT   3
#define LIBBSC_BLOCK_TYPE_BSC        1
#define LIBBSC_BAD_PARAM             -21
#define LIBBSC_DATA_TOO_LARGE        -22
#define LIBBSC_NOT_SEEKABLE          -23
#define LIBBSC_HEADER_CORRUPT        -25
#define LIBBSC_SIZE_DONTMATCH        -26

using namespace System;
using namespace System::IO;
using namespace System::IO::MemoryMappedFiles;
using namespace System::Runtime::InteropServices;
using namespace System::Security::Permissions;


const int CLI_BLOCK_HEADER_SIZE = 10; // 8 (long long) + 1 (recordSize) + 1 (sortingContexts)
const int CLI_GLOBAL_HEADER_SIZE = 4; // 'b' 's' 'c' 0x31

typedef struct BSC_BLOCK_HEADER
{
    long long       blockOffset;
    signed char     recordSize;
    signed char     sortingContexts;
} BSC_BLOCK_HEADER;



int BscDotNet::Compressor::CompressOmp(Stream^ inputStream, Stream^ outputStream, int blockSize, int NumThreads, int lzpHashSize, int lzpMinLen, int blockSorter, int coder)
{
    // Checks
    if (inputStream == nullptr || outputStream == nullptr) return LIBBSC_BAD_PARAM;
    if (!inputStream->CanRead || !outputStream->CanWrite || !inputStream->CanSeek) return LIBBSC_NOT_SEEKABLE;

    // Default values
    if (lzpHashSize == 0) lzpHashSize = 16;
    if (lzpMinLen == 0) lzpMinLen = 128;
    int features = LIBBSC_DEFAULT_FEATURES;
    signed char sortingContexts = LIBBSC_CONTEXTS_FOLLOWING; // or bsc auto detect
    //if (blockSorter == 0) blockSorter = LIBBSC_BLOCKSORTER_BWT;
    //if (coder == 0) coder = LIBBSC_CODER_QLFC_STATIC;
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

        int compressedSize = bsc_compress(buffer, outBuffer, currentBlockSize, lzpHashSize, lzpMinLen, blockSorter, coder, features);            
        // Store error
        if (compressedSize < 0)
        {
            if (compressionError == LIBBSC_NO_ERROR)
                compressionError = compressedSize;
            break;
        }

        // Write
#pragma omp critical(output_stream)
        {
            // Même code optimisé d'écriture avec finalBlock
            array<Byte>^ finalBlock = gcnew array<Byte>(10 + compressedSize);
            array<Byte>^ offsetBytes = BitConverter::GetBytes(blockOffset);
            Array::Copy(offsetBytes, 0, finalBlock, 0, 8);
            finalBlock[8] = (Byte)recordSize;
            finalBlock[9] = (Byte)sortingContexts;
            Marshal::Copy(IntPtr(outBuffer), finalBlock, 10, compressedSize);
            outputStream->Write(finalBlock, 0, 10 + compressedSize);
        }
    }
    if (compressionError != LIBBSC_NO_ERROR)
        return compressionError;
    return LIBBSC_NO_ERROR;
}


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

/************************************/
// Single block simple implementation
/************************************/
int BscDotNet::Compressor::Compress(Stream^ inputStream, Stream^ outputStream, int compressionLevel)
{
    if (inputStream == nullptr || outputStream == nullptr)
        return LIBBSC_BAD_PARAM;
    if (!inputStream->CanRead || !outputStream->CanWrite)
        return LIBBSC_BAD_PARAM;
    if (!inputStream->CanSeek)
        return LIBBSC_NOT_SEEKABLE;
    bsc_init(LIBBSC_DEFAULT_FEATURES);
    // --- Parameters ---
    int lzpHashSize = 16;
    int lzpMinLen = 128;
    int blockSorter = LIBBSC_BLOCKSORTER_BWT;
    int coder = LIBBSC_CODER_QLFC_STATIC;
    int features = LIBBSC_DEFAULT_FEATURES;
    switch (compressionLevel) {
    case 0: coder = LIBBSC_CODER_QLFC_FAST; break;
    case 1: coder = LIBBSC_CODER_QLFC_STATIC; break;
    case 2: coder = LIBBSC_CODER_QLFC_ADAPTIVE; break;
    default: return LIBBSC_COMPLVL_OUTRANGE;
    }
    // --- Get input length ---
    long long inputLengthLong = inputStream->Length;
    if (inputLengthLong > INT_MAX) return LIBBSC_DATA_TOO_LARGE;
    int inputLength = (int)inputLengthLong;
    inputStream->Position = 0;
    // --- Prepare output buffer with margin ---
    int outputCapacity = inputLength + 12 + 1024;
    array<Byte>^ outputBuffer = gcnew array<Byte>(outputCapacity);
    pin_ptr<Byte> outPtr = &outputBuffer[0];
    unsigned char* payloadPtr = outPtr + 12;
    // --- Handle input (zero-copy for MemoryStream, copy otherwise) ---
    unsigned char* inRawPtr = nullptr;
    array<Byte>^ inputBuffer = nullptr;
    pin_ptr<Byte> inPinnedPtr = nullptr;
    try {
        // ----- MemoryStream (zero-copy) -----
        MemoryStream^ memStream = dynamic_cast<MemoryStream^>(inputStream);
        if (memStream != nullptr) {
            inputBuffer = memStream->GetBuffer();
            inPinnedPtr = &inputBuffer[0];
            inRawPtr = (unsigned char*)inPinnedPtr;
        }
        else {
            // ----- Fallback (copy) for all other streams, including FileStream -----
            inputBuffer = gcnew array<Byte>(inputLength);
            int read = 0;
            while (read < inputLength) {
                int n = inputStream->Read(inputBuffer, read, inputLength - read);
                if (n == 0) return LIBBSC_UNEXPECTED_EOB;
                read += n;
            }
            inPinnedPtr = &inputBuffer[0];
            inRawPtr = (unsigned char*)inPinnedPtr;
        }
        // ---------- Compress ----------
        int compressedPayloadSize = bsc_compress(
            inRawPtr, payloadPtr, inputLength,
            lzpHashSize, lzpMinLen, blockSorter, coder, features);
        if (compressedPayloadSize <= 0)
            return compressedPayloadSize;
        // --- Write header ---
        outPtr[0] = 'b'; outPtr[1] = 's'; outPtr[2] = 'c'; outPtr[3] = '1';
        *(long long*)(outPtr + 4) = inputLengthLong;
        int totalSize = 12 + compressedPayloadSize;
        // --- Write to output stream ---
        outputStream->Write(outputBuffer, 0, totalSize);
        return totalSize;
    }
    finally {
        // Nettoyage
        if (inPinnedPtr != nullptr) inPinnedPtr = nullptr;
        if (inputBuffer != nullptr) delete inputBuffer;
    }
}

int BscDotNet::Compressor::Decompress(Stream^ inputStream, Stream^ outputStream)
{
    if (inputStream == nullptr || outputStream == nullptr)
        return LIBBSC_BAD_PARAM;
    if (!inputStream->CanRead || !outputStream->CanWrite)
        return LIBBSC_BAD_PARAM;
    if (!inputStream->CanSeek)
        return LIBBSC_NOT_SEEKABLE;
    if (inputStream->Length < 12)
        return LIBBSC_UNEXPECTED_EOB;
    // --- Read header (12 bytes) ---
    array<Byte>^ header = gcnew array<Byte>(12);
    inputStream->Position = 0;
    int read = inputStream->Read(header, 0, 12);
    if (read < 12) return LIBBSC_UNEXPECTED_EOB;
    pin_ptr<Byte> hPtr = &header[0];
    if (hPtr[0] != 'b' || hPtr[1] != 's' || hPtr[2] != 'c' || hPtr[3] != '1')
        return LIBBSC_NOT_SUPPORTED;
    long long expectedSize = *(long long*)(hPtr + 4);
    if (expectedSize < 0 || expectedSize > INT_MAX)
        return LIBBSC_DATA_CORRUPT;
    int outputSize = (int)expectedSize;
    // --- Payload size ---
    long long payloadSizeLong = inputStream->Length - 12;
    if (payloadSizeLong <= 0 || payloadSizeLong > INT_MAX)
        return LIBBSC_UNEXPECTED_EOB;
    int payloadSize = (int)payloadSizeLong;
    // --- Input pointor ---
    unsigned char* inRawPtr = nullptr;
    array<Byte>^ payloadBuffer = nullptr;
    pin_ptr<Byte> inPinnedPtr = nullptr;
    // --- Output pointor ---
    unsigned char* outRawPtr = nullptr;
    array<Byte>^ outputBuffer = nullptr;
    pin_ptr<Byte> outPinnedPtr = nullptr;
    bool useDirectOutput = false;
    try {
        // ========== INPUT : zero copy if MemoryStream ==========
        MemoryStream^ memIn = dynamic_cast<MemoryStream^>(inputStream);
        if (memIn != nullptr) {
            array<Byte>^ fullBuffer = memIn->GetBuffer();
            pin_ptr<Byte> pin = &fullBuffer[0]; // Épingle
            inRawPtr = (unsigned char*)(pin + 12); // Skip header
        }
        else {
            // Fallback : copy payload for other stream type
            payloadBuffer = gcnew array<Byte>(payloadSize);
            inputStream->Position = 12;
            read = inputStream->Read(payloadBuffer, 0, payloadSize);
            if (read < payloadSize) return LIBBSC_UNEXPECTED_EOB;
            inPinnedPtr = &payloadBuffer[0];
            inRawPtr = (unsigned char*)inPinnedPtr;
        }
        // ========== OUTPUT : no copy if MemoryStream ==========
        MemoryStream^ memOut = dynamic_cast<MemoryStream^>(outputStream);
        if (memOut != nullptr) {
            // Redimensionner le MemoryStream
            if (memOut->Capacity < outputSize)
                memOut->Capacity = outputSize;
            memOut->SetLength(outputSize);
            memOut->Position = 0;
            array<Byte>^ outBuf = memOut->GetBuffer();
            outPinnedPtr = &outBuf[0];
            outRawPtr = (unsigned char*)outPinnedPtr;
            useDirectOutput = true;
        }
        else {
            // Fallback : temp buffer
            outputBuffer = gcnew array<Byte>(outputSize);
            outPinnedPtr = &outputBuffer[0];
            outRawPtr = (unsigned char*)outPinnedPtr;
        }
        // ========== Decompress ==========
        bsc_init(LIBBSC_DEFAULT_FEATURES);
        int rc = bsc_decompress(
            inRawPtr,
            payloadSize,
            outRawPtr,
            outputSize,
            LIBBSC_DEFAULT_FEATURES);
        if (rc != LIBBSC_NO_ERROR)
            return rc;
        // ========== Rewrite final output ==========
        if (!useDirectOutput) {
            outputStream->Write(outputBuffer, 0, outputSize);
        }
        return LIBBSC_NO_ERROR;
    }
    finally {
        // Cleanup
        if (inPinnedPtr != nullptr) inPinnedPtr = nullptr;
        if (outPinnedPtr != nullptr) outPinnedPtr = nullptr;
        if (payloadBuffer != nullptr) delete payloadBuffer;
        if (outputBuffer != nullptr && !useDirectOutput) delete outputBuffer;
    }
}