#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    // Sprawdzenie liczby argumentów
    if (argc < 3) {
        fprintf(stderr, "Użycie: %s <src> <dst>\n", argv[0]);
        return 1;
    }

    //Pobranie argumentów
    char *src = argv[1];
    char *dst = argv[2];

    // stat - przechowuje infmacje o pliku st
    struct stat st;

    // Sprawdzenie czy src istnieje lub błąd
    if (stat(src, &st) != 0) {
        fprintf(stderr, "Błąd: nie można odczytać ścieżki %s\n", src);
        return 1;
    }
    // makro S_ISDIR sprawdza czy to katalog
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Błąd: %s nie jest katalogiem\n", src);
        return 1;
    }

    // Sprawdzenie czy dst istnieje lub błąd
    if (stat(dst, &st) != 0) {
        fprintf(stderr, "Błąd: nie można odczytać ścieżki %s\n", dst);
        return 1;
    }
    // makro S_ISDIR sprawdza czy to katalog
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Błąd: %s nie jest katalogiem\n", dst);
        return 1;
    }

    printf("OK: src=%s dst=%s\n", src, dst);
    return 0;
}