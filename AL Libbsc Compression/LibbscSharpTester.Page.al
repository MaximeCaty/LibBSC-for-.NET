page 50100 "LibbscSharp Tester"
{
    Caption = 'LibbscSharp Tester';
    PageType = Card;
    ApplicationArea = All;
    UsageCategory = Administration;
    SourceTable = "LibbscSharp Setup";

    layout
    {
        area(Content)
        {
            group(AzureSetup)
            {
                field("Azure Function URL"; Rec."Azure Function URL")
                {

                }
                field("Key"; Rec."Key")
                {

                }
            }
            group(Inputfile)
            {
                field(FileName; FileName)
                {
                    Editable = false;

                    trigger OnDrillDown()
                    var
                        InStr: InStream;
                        CompInStr: InStream;
                        OutStr: OutStream;
                        StartDT: DateTime;
                        Win: Dialog;
                        DataComp: Codeunit "Data Compression";
                    begin
                        ClearAll();
                        if not UploadIntoStream('Upload a file to compress', '', '', FileName, InStr) then
                            exit;
                        if InStr.Length = 0 then
                            exit;
                        OriginalSize := InStr.Length;

                        /**** GZip ****/
                        TestGZip(InStr, DurGZ, CompressedSizeGZ, DecompDurGZ);
                        CompRatioGZ := round((1 - (CompressedSizeGZ / OriginalSize)) * 100, 0.1);

                        /***** BSC Azure *****/
                        if (Rec."Azure Function URL" <> '') and (Rec."Key" <> '') then begin
                            TestBSCAzure(InStr, DurBSCAzure, CompressedSizeBSCAzure, DecompDurBSCAzure);
                            CompRatioBSCAzure := round((1 - (CompressedSizeBSC / OriginalSize)) * 100, 0.1);
                        end else begin
                            /***** BSC DLL *****/
                            TestBSC(InStr, DurBSC, CompressedSizeBSC, DecompDurBSC);
                            CompRatioBSC := round((1 - (CompressedSizeBSC / OriginalSize)) * 100, 0.1);
                        end;
                    end;
                }

                field(OriginalSize; OriginalSize)
                {
                    Caption = 'Original Size';
                    Editable = false;
                }
            }

            group(Compression1)
            {
                Caption = 'LIBBSC Azure Function Compression';

                field(DurAzure; DurBSCAzure)
                {
                    Caption = 'Comp. Duration';
                    Editable = false;
                }
                field(DecompDurAzure; DecompDurBSCAzure)
                {
                    Caption = 'Decomp. Duration';
                    Editable = false;
                }
                field(CompressedSizeAzure; CompressedSizeBSCAzure)
                {
                    Caption = 'Compressed size';
                    Editable = false;
                }
                field(CompRatioAzure; CompRatioBSCAzure)
                {
                    Caption = 'Compression ratio (%)';
                    Editable = false;
                }
            }
            group(Compression2)
            {
                Caption = 'LIBBSC DLL Compression';

                field(Dur; DurBSC)
                {
                    Caption = 'Comp. Duration';
                    Editable = false;
                }
                field(DecompDur; DecompDurBSC)
                {
                    Caption = 'Decomp. Duration';
                    Editable = false;
                }
                field(CompressedSize; CompressedSizeBSC)
                {
                    Caption = 'Compressed size';
                    Editable = false;
                }
                field(CompRatio; CompRatioBSC)
                {
                    Caption = 'Compression ratio (%)';
                    Editable = false;
                }
            }
            group(CompressionGZ)
            {
                Caption = 'GZ Compression';

                field(DurGZ; DurGZ)
                {
                    Caption = 'Duration';
                    Editable = false;
                }
                field(DecompDurGZ; DecompDurGZ)
                {
                    Caption = 'Decomp. Duration';
                    Editable = false;
                }
                field(CompressedSizeGZ; CompressedSizeGZ)
                {
                    Caption = 'Compressed size';
                    Editable = false;
                }
                field(CompRatioGZ; CompRatioGZ)
                {
                    Caption = 'Compression ratio (%)';
                    Editable = false;
                }
            }
        }
    }

    trigger OnOpenPage()
    begin
        if not Rec.Get then
            Rec.Insert();
    end;

    var
        FileName: Text;
        DecompFileName: Text;
        BSCCompression: Codeunit "BSC Data Compression";
        OriginalSize: Integer;
        CompressedSizeGZ, CompressedSizeBSC, CompressedSizeBSCAzure : Integer;
        CompRatioGZ, CompRatioBSC, CompRatioBSCAzure : Decimal;
        DurGZ, DurBSC, DurBSCAzure : Duration;
        DecompDurGZ, DecompDurBSC, DecompDurBSCAzure : Duration;

    procedure TestGZip(InputStrToCompress: InStream; var CompDuration: Duration; var CompressedSize: Integer; var DecompDuration: Duration)
    var
        Win: Dialog;
        CompFile, DecompFile : Codeunit "Temp Blob";
        DataComp: Codeunit "Data Compression";
        CompOutStr, DecompOutStr : OutStream;
        CompInStr: InStream;
        StartDT: DateTime;
    begin
        // Compress
        InputStrToCompress.ResetPosition();
        CompFile.CreateOutStream(CompOutStr);
        Win.Open('Compressing using GZip...');
        StartDT := CurrentDateTime;
        DataComp.GZipCompress(InputStrToCompress, CompOutStr);
        CompDuration := CurrentDateTime - StartDT;
        Win.Close();
        CompressedSize := CompFile.Length();

        // Decompress
        CompFile.CreateInStream(CompInStr);
        DecompFile.CreateOutStream(DecompOutStr);
        Win.Open('Decompressing using GZip...');
        StartDT := CurrentDateTime;
        DataComp.GZipDecompress(CompInStr, DecompOutStr);
        DecompDuration := CurrentDateTime - StartDT;
        Win.Close();
    end;

    procedure TestBSC(InputStrToCompress: InStream; var CompDuration: Duration; var CompressedSize: Integer; var DecompDuration: Duration)
    var
        Win: Dialog;
        CompFile, DecompFile : Codeunit "Temp Blob";
        CompOutStr, DecompOutStr : OutStream;
        CompInStr: InStream;
        StartDT: DateTime;
    begin
        // Compress
        InputStrToCompress.ResetPosition();
        CompFile.CreateOutStream(CompOutStr);
        Win.Open('Compressing using LIBBSC...');
        StartDT := CurrentDateTime;
        BSCCompression.Compress(InputStrToCompress, CompOutStr, 1);
        CompDuration := CurrentDateTime - StartDT;
        Win.Close();
        CompressedSize := CompFile.Length();

        // Decompress
        CompFile.CreateInStream(CompInStr);
        CompInStr.ResetPosition();
        DecompFile.CreateOutStream(DecompOutStr);
        Win.Open('Decompressing using LIBBSC...');
        StartDT := CurrentDateTime;
        BSCCompression.Decompress(CompInStr, DecompOutStr);
        DecompDuration := CurrentDateTime - StartDT;
        Win.Close();
    end;

    procedure TestBSCAzure(InputStrToCompress: InStream; var CompDuration: Duration; var CompressedSize: Integer; var DecompDuration: Duration)
    var
        Win: Dialog;
        CompFile, DecompFile : Codeunit "Temp Blob";
        DataComp: Codeunit "BSC Data Compression";
        CompOutStr, DecompOutStr : OutStream;
        CompInStr: InStream;
        StartDT: DateTime;
    begin
        // Compress
        InputStrToCompress.ResetPosition();
        CompFile.CreateOutStream(CompOutStr);
        Win.Open('Compressing using LIBBSC...');
        StartDT := CurrentDateTime;
        DataComp.AzureCompress(Rec."Azure Function URL", Rec."Key", InputStrToCompress, CompOutStr, 1);
        CompDuration := CurrentDateTime - StartDT;
        Win.Close();
        CompressedSize := CompFile.Length();

        // Decompress
        CompFile.CreateInStream(CompInStr);
        CompInStr.ResetPosition();
        DecompFile.CreateOutStream(DecompOutStr);
        Win.Open('Decompressing using LIBBSC...');
        StartDT := CurrentDateTime;
        DataComp.AzureDecompress(Rec."Azure Function URL", Rec."Key", CompInStr, DecompOutStr);
        DecompDuration := CurrentDateTime - StartDT;
        Win.Close();
    end;
}