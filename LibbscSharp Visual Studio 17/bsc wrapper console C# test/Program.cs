using BscDotNet;
using System;
using System.Diagnostics;
using System.IO;

class Program
{        
    static void Main()
    {
        
        // ===================== COMPRESSION =====================
        Console.Write("Enter a file path to compress: ");
        string inputPath = Console.ReadLine()?.Trim('"');
        if (!File.Exists(inputPath))
        {
            Console.WriteLine("Input file not found.");
            return;
        }

        string compressedPath = inputPath + ".bsc";

        Console.WriteLine($"Original file: {new FileInfo(inputPath).Length} bytes");

        var sw = Stopwatch.StartNew();

        try
        {
            using (var inputStream = new FileStream(inputPath, FileMode.Open, FileAccess.Read))
            using (var outputStream = new FileStream(compressedPath, FileMode.Create, FileAccess.Write))
            {
                int result = Compressor.Compress(inputStream, outputStream, 2);
                if (result <= 0)
                {
                    Console.WriteLine($"Compression failed with code: {result}");
                    return;
                }
                Console.WriteLine($"Compressed size: {new FileInfo(compressedPath).Length} bytes");
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error during compression: {ex.Message}");
            return;
        }

        sw.Stop();
        Console.WriteLine($"Compression took: {sw.ElapsedMilliseconds} ms");
        Console.WriteLine($"File saved to: {compressedPath}");

        // ===================== DECOMPRESSION =====================
        Console.Write("\nEnter file path to decompress: ");
        string decompressInput = Console.ReadLine()?.Trim('"');
        if (!File.Exists(decompressInput))
        {
            Console.WriteLine("Input file not found.");
            return;
        }

        string decompressedPath = Path.ChangeExtension(decompressInput, ".dec");

        sw = Stopwatch.StartNew();

        try
        {
            using (var inputStream = new FileStream(decompressInput, FileMode.Open, FileAccess.Read))
            using (var outputStream = new FileStream(decompressedPath, FileMode.Create, FileAccess.Write))
            {
                int result = Compressor.Decompress(inputStream, outputStream);
                if (result < 0)
                {
                    Console.WriteLine($"Decompression failed with code: {result}");
                    return;
                }
                Console.WriteLine($"Decompressed size: {new FileInfo(decompressedPath).Length} bytes");
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error during decompression: {ex.Message}");
            return;
        }

        sw.Stop();
        Console.WriteLine($"Decompression took: {sw.ElapsedMilliseconds} ms");
        Console.WriteLine($"File saved to: {decompressedPath}");

        Console.WriteLine("\nPress any key to exit...");
        Console.ReadKey();
    }
}
