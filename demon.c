#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <syslog.h>

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
        syslog(LOG_INFO, "Demon spi...");
        sleep(10);
        syslog(LOG_INFO, "Demon sie obudzil");
        // tutaj będzie synchronizacja
    }

    closelog();
    return 0;
}