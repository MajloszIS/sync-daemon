#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <syslog.h>
#include <dirent.h>

void create_daemon() {
    pid_t pid; // zmienna dla PID procesu

    // Kopia procsesu i zakonczenie procesu rodzica lub bledu
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    // Dziecko tworzy nową sesję - odłącza się od terminala
    if (setsid() < 0) exit(EXIT_FAILURE);

    // Zabezpieczenie - proces nie może z powrotem przejąć żadnego terminala - sesja zostaje bez lidera
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    // Zmiana katalogu roboczego na /
    chdir("/");

    // zamknięcie zbędnych deskryptorów stdin/stdout/stderr
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

void sync_dirs(const char *src, const char *dst) {

    DIR *dir;
    struct dirent *entry;    // struktura przechowująca informacje o jednym wpisie
    struct stat src_stat, dst_stat;
    char src_path[1024];
    char dst_path[1024];

    // przechodzimy po plikach w src
    dir = opendir(src); // otwieramy katalog - zwraca "uchwyt" do katalogu
    if (dir == NULL) {
        syslog(LOG_ERR, "Cannot open source directory: %s", src);
        return;
    }

    // readdir() zwraca kolejny wpis, NULL gdy koniec
    // DIR pamieta o aktualnej pozycji w katalogu, wiec mozemy czytac kolejne wpisy
    while ((entry = readdir(dir)) != NULL) {
        // ignorujemy wszystko co nie jest zwykłym plikiem
        if (entry->d_type != DT_REG) continue;

        // budujemy pełne ścieżki
        // d_name to nazwa pliku/katalogu
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);

        // pobieramy informacje o pliku w src
        if (stat(src_path, &src_stat) != 0) {
            syslog(LOG_ERR, "Cannot stat: %s", src_path);
            continue;
        }

        // sprawdzamy czy plik istnieje w dst
        if (stat(dst_path, &dst_stat) != 0) {
            // plik nie istnieje w dst - trzeba skopiować
            syslog(LOG_INFO, "New file, copying: %s", entry->d_name);
            // tutaj będzie kopiowanie
        } else {
            // plik istnieje - porównujemy daty modyfikacji
            if (src_stat.st_mtime > dst_stat.st_mtime) {
                syslog(LOG_INFO, "File modified, copying: %s", entry->d_name);
                // tutaj będzie kopiowanie
            }
        }
    }

    closedir(dir);

    // przechodzimy po plikach w dst
    dir = opendir(dst);
    if (dir == NULL) {
        syslog(LOG_ERR, "Cannot open dest directory: %s", dst);
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG) continue;

        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);

        // sprawdzamy czy plik istnieje w src
        if (stat(src_path, &src_stat) != 0) {
            // plik nie istnieje w src - usuwamy z dst
            syslog(LOG_INFO, "File removed from src, deleting: %s", entry->d_name);
            // tutaj będzie usuwanie
        }
    }

    closedir(dir);
}

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

    // Po walidacji - uruchamiamy demona
    create_daemon();

    // konfiguracja sysloga i logowanie informacji o uruchomieniu demona
    openlog("sync-daemon", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Demon uruchomiony src=%s dst=%s", src, dst);

    // Główna pętla
    while (1) {
        syslog(LOG_INFO, "Demon sie obudzil - sprawdzam katalogi");
        sync_dirs(src, dst);
        syslog(LOG_INFO, "Demon spi...");
        sleep(10);
    }

    closelog();
    return 0;
}