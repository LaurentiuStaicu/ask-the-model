namespace AskTheModel.RepositoryNative {
    [CCode (
        cname = "atm_cff_extract_version",
        cheader_filename = "cff_version.h"
    )]
    public static extern bool cff_extract_version (
        [CCode (array_length = false)] uint8[] data,
        size_t length,
        out string version
    ) throws GLib.Error;
}
