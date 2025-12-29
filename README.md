

# LibBSC Sharp

.NET Portage of the fine "Block Sorting Compression" library by Ilya Grebnov : https://github.com/IlyaGrebnov/libbsc

- .NET Core 8.0
- .NET Framework 4.8
- Azure Function with HTTP trigger for cloud usage
- Microsoft Dynamics Business Central Extension exemple


File produced are compatible with original command line tool (bsc.exe).

## What is BSC

Block sorting compression is an alternative compression method achieving much higher compression than dictionnary methods (such as Gzip, 7z, even zStandard) on some data format.
Comparably to other heavier compression like Bzip2 and Brotli, BSC offer both faster speed and better compression.


Ilya implemented a high performance version of BSC in C/C++ supporting multhread and Cuda acceleration.


It excel on reducing size of raw files with repeatable content and structured format like :
 - CSV, Flat, Excel, Json, Xml files 
 - Database Dump 
 - Binary table datas (row or column
   oriented)

It doesnt compete well :
 - Already encoded media such as JPG, PNG, GIF, MP3, ...
 - Smaller files (< 100 KB)
 - already compressed file

### Performance

You can expect 40% additionnal reduction than gzip or zstandard, for a cost of ~ 1.5 - 3x process time on fastest level.

The process time may be further reduce on larger file using multihreading.

I experienced on some file over 98% reduction when data is structured with highly repeatable numerical values at level 3.

Important note : running through Azure function or Busienss Central cause speed degradation likely due to interop overhead and http data transfer.

### Benchmark

- Gzip : Optimal level (6/9)
- ZStandard : Fast level (3/19)
- BSC : Fast level (1/3)

Setup : i7 12700KF - 32GB DDR5 running single threaded, no GPU acceleration.

| File | Size | GZ | % | Time | ZStd | % (cumulative) | Time | BSC | % (cumulative) | Time** |
|--|--|--|--|--|--|--|--|--|--|--|
| Json Exemple | 500 KB | 37 KB | -93% | 7/1 | 50 KB | no gain | 30/10 | **20 KB** | **-46%** | 16/0 - (44/16) |
| CSV 100K Records | 16.5 MB | 7.9 MB | -52% | 780/71 | 7.1 MB | -10% | 110/40 | **4.2 MB** | **-41%** | 219/125 - (1'415/548) |
| CSV 1M Records | 50.0 MB | 24.9 MB | -50% | 2960/211 | 23.1 MB | -7% | 310/70 |**12.7 MB** | **-45%** | 640/344 - (4'199/1'733) |
| Binary MySQL Employees exemple | 290 MB | 86.0 MB | -70% | 10'148/952 | 86.5 MB | no gain | 1040/260 |**55.9 MB** | **-35%** | 3'281/2'175 - (18'004/9'042) |

*Time expressed in milliseconds for Compression/Decompression

**Run from .NET - (Run from Business Central, interop overhead)

Additionnal comparaison with advanced compressor can be found here https://www.mattmahoney.net/dc/text.html  (BSC is on line 33)

----

# Usage

## DotNet
    
Add in to your project a reference to "bscwrapperCLR .NET Core" or "bscwrapperCLR .NET FrameWork".

Use **CompressOmp** to compress a stream of data
 
- @param inputStream                        - the input data to compress
- @param outputStream                       - the output compressed data including global header + blocks headers
- @param blockSize                          - the maximum block size in Byte to compress sequentially, higher value improve ratio while consuming more RAM. Default if 25 MB.
- @param NumThreads                         - the number of threads to use if the file is multy-blocks (depend on input size and block size), 0 for auto detect.
- @param lzpHashSize                        - the hash table size if LZP enabled, 0 otherwise. Must be in range [0, 10..28].
- @param lzpMinLen                          - the minimum match length if LZP enabled, 0 otherwise. Must be in range [0, 4..255].
- @param blockSorter                        - the block sorting algorithm. Must be in range [ST3..ST8, BWT].
- @param coder                              - the entropy coding algorithm. Must be in range 1..3
- @return 0 if succed, nagative value for error code

Use **DecompressOmp** to decompress a stream of BSC data
- @param inputStream                        - the compressed input data
- @param outputStream                       - the output decompressed data result
- @param numThreads                         - the number of threads to use if the file is multy-blocks, 0 for auto detect
- @return 0 if succed, nagative value for error code

     
## Azure function

### Publish Azure Function

1. Create a new Azure your function from Azure portal. 
    It must be using **windows** operating system and **target .NET Core 8.0**
3. Use the project "LibbscSharp AZ Function" included in the repo folder "LibbscSharp Visual Studio 17".
4. Create a publication Profile to your Azure account, and create a new function / group
6. Publish the project from Visual Studio
7. Find the URL from your Azure dash board -> Azure function -> Overview -> "Default Domain"
8. Create an API Key for your Azure Function under Functions -> Application Key
9. When calling the API, add an Http header "x-functions-key" with the key as value.

> Publication issue : 
Default Azure Function plan as per Dec.2025 (Flexible) run on Linux. 
Make sure to select a plan that support Windows (i.e. "Consumption").

> Runtime dependency issue :
If you modify the CLR project, you must copy the compiled DLLs manualy under the libs folder, and verify that the DLLs are copied when publishing : open "LibbscSharp AZ Function.csproj" and check for presence of 

    <None Update="lib/bscwrapperCLR .NET Core.dll">
      <CopyToOutputDirectory>Always</CopyToOutputDirectory>
      <CopyToPublishDirectory>Always</CopyToPublishDirectory>
    </None>
    <None Update="lib/Ijwhost.dll">
      <CopyToOutputDirectory>Always</CopyToOutputDirectory
      <CopyToPublishDirectory>Always</CopyToPublishDirectory>
    </None>

### API transfers compression

Compress/Decompress API support gzipped payload (Content-Encoding/Accept-encoding).

It reduce network load but slow down the process by 10-15%, so I won't recommand to use it unledd network load is critical.

### Ping
Use this function to verify you are able to reach the Azure Function.

GET url/api/PING


### Compress trigger

POST url/api/COMPRESS?coder=0&blocksize=50
 - (Mandatory parameter) coder : Compression level 1 (fast), 2 (medium), 3 (high). Default is fast.
 - (Optional parameter) blocksize : Block size in MegaBytes. Default is 25 MB.
 - Body : Raw input data to compress, can be Gzipped to speed up transfer (provide header "Content-encoding: gz")
 - Response : Compressed data using Libbsc, or error code

### Decompress trigger

POST url/api/DECOMPRESS
- No parmeters
- Body : Compressed payload to decompress
- Response : Decompressed data, or error code. Can be returned Gzipped to speed up transfer (provide header "Accept-encoding: gz")

## Business Central Cloud

Publish Azure function as above and run function with AL Http client.

The repo extension provide an exemple to do this.

>
    var
        BSCCompression: Codeunit "BSC Data Compression";
    begin
        // Compress
        BSCCompression.AzureLibbscCompress(URL, Key, InputStrToCompress, CompOutStr, 1);
        // Decompress
        BSCCompression.AzureLibbscDecompress(URL, Key, CompInStr, DecompOutStr);

## Business Central On Premise

You can use provided exemple in the repo or use the DLL manualy as following :

1. Copy DLLs in your extension under .netpackages folder + in Addin folder of Business central server instance.

Business Central V23+ :
- bscwrapperCLR .NET Core.dll
- Ijwhost.dll
- LibbscSharp Net Core.dll

For older BC / Nav version, use .Net Framework version :
- bscwrapperCLR .NET Framework.dll
- LibbscShartp.Net_Framework_4._8.dll
----

2. Restart Business Central service Instance along with VS Code (so they fetch available DLLs)
----
3. In your extension, declare the assembly 

        
        dotnet { 
          assembly("LibbscSharp Net Core") 
          { 
            type(LibbscSharp.LibscSharp; LibbscSharp) { } 
          } 
          // For older BC/NAV Version use the .Net Framework 4.8 version :
          //assembly(LibbscShartp.Net_Framework_4._8) 
          //{ //type(LibbscSharp.LibscSharp; LibbscSharp) { } 
          //} 
        }
        
----
4. Use the library from AL language to compress or decompress data :

>
    var
        Libbsc: Dotnet  LibbscSharp;
    begin
        // You must instanciate the static class
        Libbsc  :=  Libbsc.LibscSharp();
        // Compress
        Libbsc.BSCCompress(InputStreamToCompress, CompressedOutStream, 0, 0, 100  *  1024  *  1024);
        // Decompress
        Libbsc.BSCDecompress(CompInStr, DecompOutStr, 0);

5. Or with provided codeunit

>
    var
        BSCCompression: Codeunit "BSC Data Compression";
    begin
        // Compress
        BSCCompression.Compress(InputStrToCompress, CompOutStr, 1);
        // Decompress
        BSCCompression.Decompress(CompInStr, DecompOutStr);

## Error Code List

Error From Libbsc

 - -1 : LIBBSC_BAD_PARAMETER
 - -2 : LIBBSC_NOT_ENOUGH_MEMORY 
 - -3 : LIBBSC_NOT_COMPRESSIBLE
 - -4 : LIBBSC_NOT_SUPPORTED
 - -5 : LIBBSC_UNEXPECTED_EOB
 - -6 : LIBBSC_DATA_CORRUPT
 - -7 : LIBBSC_GPU_ERROR 
 - -8 : LIBBSC_GPU_NOT_SUPPORTED
 - -9 : LIBBSC_GPU_NOT_ENOUGH_MEMORY

Error from the C++ CLR Wrapper

 - -20 : LIBBSC_COMPLVL_OUTRANGE
 - -21 : LIBBSC_BAD_PARAM
 - -23 : LIBBSC_NOT_SEEKABLE
