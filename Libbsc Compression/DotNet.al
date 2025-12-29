dotnet
{
    assembly("LibbscSharp Net Core")
    {
        type(LibbscSharp.LibscSharp; LibbscSharp) { }
    }

    /*
    // For older BC/NAV Version use the .Net Framework 4.8 version :
    assembly(LibbscShartp.Net_Framework_4._8)
    {
        type(LibbscSharp.LibscSharp; LibbscSharp) { }
    }*/
}