/* CPU-only table; no WSL path is accessed by check. */
static void wsl_classifier_cases(void)
{
    const char *yes[] = {
        "\\\\wsl.localhost\\Ubuntu\\a", "\\\\WSL.LOCALHOST\\Ubuntu\\A",
        "\\\\wsl$\\Ubuntu\\space ", "\\\\WsL$\\Ubuntu\\trailing.",
        "\\\\?\\UNC\\wsl.localhost\\Ubuntu\\a", "\\\\?\\uNc\\WSL$\\Ubuntu\\A",
        "//wsl.localhost/Ubuntu/a", "//WSL$/Ubuntu/A", "//?/UNC/wsl$/Ubuntu/a",
        "\\\\.\\UNC\\wsl$\\Ubuntu\\a", "\\\\wsl.localhost", "\\\\wsl$\\", "\\\\?\\UNC\\wsl$"
    };
    const char *no[] = {
        NULL, "", "\\", "\\\\", "\\\\?", "\\\\?\\", "\\\\?\\UNC", "\\\\?\\UNC\\",
        "C:\\wsl.localhost\\a", "wsl.localhost/Ubuntu/a", "/wsl$/a",
        "\\\\server\\wsl$\\a", "\\\\wsl.localhost.example\\Ubuntu\\a",
        "\\\\wsl$extra\\Ubuntu\\a", "\\\\wsl.localhost:445\\Ubuntu\\a",
        "\\\\?\\C:\\wsl$\\a"
    };
    for (size_t i = 0; i < sizeof yes / sizeof yes[0]; i++) CHECK(os_path_is_wsl(yes[i]));
    for (size_t i = 0; i < sizeof no / sizeof no[0]; i++) CHECK(!os_path_is_wsl(no[i]));
}
