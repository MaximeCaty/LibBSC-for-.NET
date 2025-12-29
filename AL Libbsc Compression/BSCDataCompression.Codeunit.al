codeunit 50100 "BSC Data Compression"
{
    var
        Libbsc: Dotnet LibbscSharp;
        Instanciated: Boolean;

    procedure IsBSC(InputStream: InStream): Boolean
    var
        Header: Text[4];
    begin
        InputStream.ResetPosition();
        InputStream.Read(Header, 4);
        exit(Header = 'bsc1');
    end;

    #region DLL
    procedure Compress(InputStrToCompress: InStream; CompressedOutStr: OutStream; CompressionLevel: Integer)
    var
        Header: Text[4];
        UnsupportedFormat: Label 'Input data is already compressed using BSC.';
        UnsupportedCompression: Label 'Compression level %1 is not supported for Libbsc. Accepted avalues are : 1, 2, 3.';
    begin
        // Verify compression mode
        if not (CompressionLevel in [1, 2, 3]) then
            Error(UnsupportedCompression, CompressionLevel);

        // Verify data is not BSC
        InputStrToCompress.ResetPosition();
        InputStrToCompress.Read(Header, 4);
        if Header = 'bsc1' then
            Error(UnsupportedFormat);

        if not Instanciated then begin
            Libbsc := Libbsc.LibscSharp(); // instanciate static class
            Instanciated := true;
        end;

        Libbsc.BSCCompress(InputStrToCompress, CompressedOutStr, CompressionLevel, 0, 100 * 1024 * 1024);
    end;

    procedure Decompress(InputStrToDeompress: InStream; DecompressedOutStr: OutStream)
    var
        Header: Text[4];
        UnsupportedFormat: Label 'Input data is not recognized as BSC compressed payload.';
    begin
        // Verify data is not BSC
        InputStrToDeompress.ResetPosition();
        InputStrToDeompress.Read(Header, 4);
        if Header <> 'bsc1' then
            Error(UnsupportedFormat);

        if not Instanciated then begin
            Libbsc := Libbsc.LibscSharp(); // instanciate static class
            Instanciated := true;
        end;

        Libbsc.BSCDecompress(InputStrToDeompress, DecompressedOutStr, 0);
    end;
    #endregion

    #region Azure Function
    procedure AzureCompress(BaseURL: Text; APIKey: Text; InStr: InStream; OutStr: OutStream; CompressionLevel: Integer)
    begin
        AzureCompress(BaseURL, APIKey, false, InStr, OutStr, CompressionLevel);
    end;

    procedure AzureCompress(BaseURL: Text; APIKey: Text; HttpGzip: Boolean; InStr: InStream; OutStr: OutStream; CompressionLevel: Integer)
    var
        Client: HttpClient;
        HttpHeader: HttpHeaders;
        Request: HttpRequestMessage;
        Response: HttpResponseMessage;
        Content: HttpContent;
        ResponseStream: InStream;
        Headers: HttpHeaders;
        GzBlob: Codeunit "Temp Blob";
        Compress: Codeunit "Data Compression";
        GzOutStr: OutStream;
    begin
        Request.Method := 'POST';
        BaseURL := BaseURL.TrimEnd('/');
        if not BaseURL.EndsWith('/api') then
            BaseURL += '/api';
        Request.SetRequestUri(StrSubstNo('%1/COMPRESS?coder=%2', BaseURL, CompressionLevel));

        // API Key
        Request.GetHeaders(Headers);
        Headers.Add('x-functions-key', APIKey);

        // Compress input with gzip first to speed up transfer
        if HttpGzip then begin
            GzBlob.CreateOutStream(GzOutStr);
            Compress.GZipCompress(InStr, GzOutStr);
            clear(InStr);
            GzBlob.CreateInStream(InStr);
        end;

        // Content-Type
        Content.WriteFrom(InStr);
        Content.GetHeaders(HttpHeader);
        HttpHeader.TryAddWithoutValidation('Content-Type', 'application/octet-stream');
        if HttpGzip then
            HttpHeader.Add('Content-Encoding', 'gz');

        // Send
        Request.Content := Content;
        Client.Send(Request, Response);

        if not Response.IsSuccessStatusCode() then
            Error('HTTP Error (%1)', Response.HttpStatusCode());
        if Response.IsBlockedByEnvironment then
            Error('Http request blocked by environment.');

        // Copy response to out stream
        Response.Content().ReadAs(ResponseStream);
        CopyStream(OutStr, ResponseStream);
    end;

    procedure AzureDecompress(BaseURL: Text; APIKey: Text; InStr: InStream; OutStr: OutStream)
    begin
        AzureDecompress(BaseURL, APIKey, false, InStr, OutStr);
    end;

    procedure AzureDecompress(BaseURL: Text; APIKey: Text; HttpGzip: Boolean; InStr: InStream; OutStr: OutStream)
    var
        Client: HttpClient;
        HttpHeader: HttpHeaders;
        Request: HttpRequestMessage;
        Response: HttpResponseMessage;
        Content: HttpContent;
        ResponseStream: InStream;
        Headers: HttpHeaders;
        Compress: Codeunit "Data Compression";
    begin
        Request.Method := 'POST';
        BaseURL := BaseURL.TrimEnd('/');
        if not BaseURL.EndsWith('/api') then
            BaseURL += '/api';
        Request.SetRequestUri(StrSubstNo('%1/DECOMPRESS', BaseURL));

        // API Key
        Request.GetHeaders(Headers);
        Headers.Add('x-functions-key', APIKey);

        // Content-Type
        Content.WriteFrom(InStr);
        Content.GetHeaders(HttpHeader);
        HttpHeader.TryAddWithoutValidation('Content-Type', 'application/octet-stream');

        // Send
        Request.Content := Content;
        Request.GetHeaders(HttpHeader);
        if HttpGzip then
            HttpHeader.Add('Accept-Encoding', 'gzip, gz');
        Client.Send(Request, Response);

        if not Response.IsSuccessStatusCode() then
            Error('HTTP Error (%1)', Response.HttpStatusCode());
        if Response.IsBlockedByEnvironment then
            Error('Http request blocked by environment.');

        // Copy response to out stream
        Response.Content().ReadAs(ResponseStream);
        if Compress.IsGZip(ResponseStream) then
            Compress.GZipDecompress(ResponseStream, OutStr)
        else
            CopyStream(OutStr, ResponseStream);
    end;
    #endregion
}
