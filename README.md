
# LibBSC Sharp

.NET Portage of the fine "Block Sorting Compression" library by Ilya Grebnov : https://github.com/IlyaGrebnov/libbsc

- .NET Core 8.0
- .NET Framework 4.8
- Azure Function with HTTP trigger for cloud usage
- Microsoft dynamics Business Central integration exemple

Produce compatible file with original command line tool (bsc.exe).


## What is BSC

Block sorting compression is a method achieving much higher compression than dictionnary (such as Gzip, 7z, even zStandard) on "industrial" file but is often very slow.
Ilya implemented a high performance version of BSC in C/C++ that also support Cuda for GPU acceleration.


It excel on reducing size of "industrial" files with repeatable content and structured format like :
 - CSV, Flat, Excel, Json, Xml files 
 - Database Dump 
 - Binary table datas (row or column
   oriented)

It doesnt perform great on :
 - Already encoded media such as JPG, PNG, GIF, MP3, ...
 - Smaller files (< 100 KB)


### Comparisons

Setup : i7 12700KF - 32GB DDR5

Gzip : Fast level (2/9)
BSC : Fast level (1/3), single thread
ZStandard : Fast level (3), single thread

| File | Size | GZ | % | Time | ZStd | % (cumul) | Time | BSC | % (cumul) | Time** |
|--|--|--|--|--|--|--|--|--|--|--|
| CSV 100K Records | 16.5 MB | 7.9 MB | -52% | 780/71 | 7.1 MB | -10% | 110/40 | **4.2 MB** | **-41%** | 219/125 - (1'415/548) |
| CSV 1M Records | 50.0 MB | 24.9 MB | -50% | 2960/211 | 23.1 MB | -7% | 310/70 |**12.7 MB** | **-45%** | 640/344 - (4'199/1'733) |
| Binary MySQL Employees exemple | 290 MB | 86.0 MB | -70% | 10'148/952 | 86.5 MB | +0% | 1040/260 |**55.9 MB** | **-35%** | 3'281/2'175 - (18'004/9'042) |

** run from .NET - (run from Business Central, adding interop overhead and does not support multithreading neither Cuda)

Additionnal comparaison with advanced compressor can be found here https://www.mattmahoney.net/dc/text.html  (BSC is on line 33)

# Usage

## Compression
    
**Dot Net** 

"BSCCompress" call the ported C++ CLR method with following arguments :

 - inputStream : Input data stream to compress 
 - outputStream : Result compressed stream 
 - CompressionLevel : 1 (fast), 2 (medium), 3 (high)
 - Numthreads : number of thread used by OpenMP when the file size is
   larger than blocksize only. Leave 0 to let the system detect the
   number of core. 
 - BlockSize : Maximum size proceed in a row by the
   process, in Bytes. Larger block size mean better compression ration,
   but more RAM usage and less benefit from multithreading. Default is
   50 MB.

     
> BSCCompress(Stream inputStream, Stream outputStream, int CompressionLevel, int Numthreads, int BlobkSize)

**Azure function**

POST url/api/COMPRESS?coder=0&blocksize=50
Optionnal parmeters :
 - coder : Compression level 1 (fast), 2 (medium), 3 (high), default is fast
 - blocksize : Block size in MegaBytes, default is 

Body : Input data to compress
Response : Compressed data, or error code

**Business Central On Premise**

1. Put the dll in your extension under .netpackages folder 
2. Put the dll in Addin folder of Business central server instance, restart the service
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
4. Use the library from AL language

>
    var
        Libbsc: Dotnet  LibbscSharp;
        ...
    begin
        // You must instanciate the static class
        Libbsc  :=  Libbsc.LibscSharp();
        // Compress
        Libbsc.BSCCompress(InputStreamToCompress, CompressedOutStream, 0, 0, 100  *  1024  *  1024);
    

## Decompression

**Dot Net** 

"BSCDecompress" call the ported C++ CLR method with following arguments :

 - inputStream : Input compressed data stream 
 - outputStream : Result decompressed stream 
 - Numthreads : number of thread used by OpenMP when the file has multiple blocks. Leave 0 to let the system detect the
   number of core. 

     
> BSCDecompress(Stream inputStream, Stream outputStream, int Numthreads)

**Azure function**

POST url/api/DECOMPRESS
No parmeters
Body : Compressed payload to decompress
Response : Decompressed data, or error code

**Business Central On Premise**


    var
        Libbsc: Dotnet  LibbscSharp;
        ...
    begin
        // You must instanciate the static class
        Libbsc  :=  Libbsc.LibscSharp();
        // Decompress
        Libbsc.BSCDecompress(CompInStr, DecompOutStr, 0);
