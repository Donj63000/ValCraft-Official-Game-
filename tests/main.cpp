#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

int main(int argc, char** argv) {
#if defined(_MSC_VER) && defined(_DEBUG)
    // Je redirige les assertions du runtime vers la console pour que la CI
    // échoue avec un diagnostic exploitable au lieu d'attendre une boîte
    // de dialogue interactive.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    doctest::Context context(argc, argv);
    return context.run();
}
