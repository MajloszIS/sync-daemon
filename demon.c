#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <syslog.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>

#define BUFFER_SIZE 4096
#define DEFAULT_THRESHOLD 1048576

// Funkcja tworząca demona
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

// kopiowanie pliku z src do dst, ustawienie daty modyfikacji dst takiej samej jak src
void copy_file(const char *src_path, const char *dst_path, struct stat *src_stat, off_t threshold) {
    // otwieramy plik źródłowy tylko do odczytu
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        syslog(LOG_ERR, "Cannot open source file: %s", src_path);
        return;
    }

    // otwieramy/tworzymy plik docelowy do zapisu
    // 0644     - uprawnienia: właściciel czyta/pisze, reszta tylko czyta
    int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        syslog(LOG_ERR, "Cannot open dest file: %s", dst_path);
        close(src_fd);
        return;
    }

    // rozmiar pliku z src_stat
    off_t file_size = src_stat->st_size;

    if (file_size > threshold) {
        // DUŻY PLIK - używamy mmap
        syslog(LOG_INFO, "Using mmap for: %s", src_path);

        // mapujemy cały plik źródłowy do pamięci
        // PROT_READ - tylko do odczytu
        // MAP_SHARED - współdzielona mapa
        void *map = mmap(NULL, file_size, PROT_READ, MAP_SHARED, src_fd, 0);
        if (map == MAP_FAILED) {
            syslog(LOG_ERR, "mmap failed for: %s", src_path);
            close(src_fd);
            close(dst_fd);
            return;
        }

        // zapisujemy całą mapę do pliku docelowego jednym wywołaniem write
        write(dst_fd, map, file_size);

        // zwalniamy mapę z pamięci
        munmap(map, file_size);
    } else {
        // MAŁY PLIK - używamy bufora
        syslog(LOG_INFO, "Using buffer for: %s", src_path);

        // bufor - tymczasowe miejsce w pamięci na dane
        char buffer[BUFFER_SIZE];
        ssize_t bytes_read;

        // czytamy kawałkami i zapisujemy
        while ((bytes_read = read(src_fd, buffer, BUFFER_SIZE)) > 0) {
            write(dst_fd, buffer, bytes_read);
        }
    }
    

    close(src_fd);
    close(dst_fd);

    // ustawiamy datę modyfikacji dst taką samą jak src
    struct timespec times[2];
    times[0] = src_stat->st_atim; // czas dostępu
    times[1] = src_stat->st_mtim; // czas modyfikacji

    utimensat(AT_FDCWD, dst_path, times, 0);

    syslog(LOG_INFO, "Copied: %s", src_path);
}

//usuwanie nadmiarowych plików z dst
void delete_file(const char *path) {
    if (unlink(path) != 0) {
        syslog(LOG_ERR, "Cannot delete file: %s", path);
        return;
    }
    syslog(LOG_INFO, "Deleted: %s", path);
}

// usuwanie calego katalogu z zawartoscia (dla opcji rekurencyjnej)
void delete_dir_recursively(const char *path) {
    DIR *dir = opendir(path);
    if (dir == NULL) return;
    struct dirent *entry;
    char buf[1024];

    // przechodzimy po zawartosci katalogu
    while ((entry = readdir(dir)) != NULL) {
        // ignorujemy . i .. żeby nie cofnąć się w systemie
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        snprintf(buf, sizeof(buf), "%s/%s", path, entry->d_name);
        
        if (entry->d_type == DT_DIR) {
            // jesli w srodku jest kolejny katalog, funkcja wywoluje sama siebie zeby go wyczyscic
            delete_dir_recursively(buf);
        } else {
            // jesli to zwykly plik, po prostu go usuwamy
            delete_file(buf);
        }
    }
    closedir(dir);
    rmdir(path); // usuniecie samego katalogu na koniec
    syslog(LOG_INFO, "Deleted dir: %s", path);
}

// synchronizacja katalogów src i dst
void sync_dirs(const char *src, const char *dst, int recursive, off_t threshold) {

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
    while ((entry = readdir(dir)) != NULL)
    {
        // sprawdzamy czy trafiliśmy na podkatalog
        if (entry->d_type == DT_DIR) {
            // rekurencyjne dodawanie plików i/lub folderów
            // pomijamy go jesli flaga -R jest wyłączona ALBO jest to katalog . lub ..
            if (!recursive || strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
            snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);

            if (stat(src_path, &src_stat) == 0) {
                // tworzymy katalog w dst jesli jeszcze go nie ma
                if (stat(dst_path, &dst_stat) != 0) {
                    mkdir(dst_path, src_stat.st_mode); 
                    syslog(LOG_INFO, "Created dir: %s", dst_path);
                }
                // funkcja zatrzymuje się, wchodzi do podkatalogu
                sync_dirs(src_path, dst_path, recursive, threshold);
            }
            // instrukcja continue zeby pętla zignorowała ten wpis i poszła do kolejnego pliku/katalogu
            continue; 
        }

        // ignorujemy wszystko co nie jest zwykłym plikiem
        if (entry->d_type != DT_REG) continue;

        // nie rekurencyjne dodawanie plików
        // budujemy pełne ścieżki
        // d_name to nazwa pliku/katalogu
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);

        // pobieramy informacje o pliku w src
        if (stat(src_path, &src_stat) != 0)
        {
            syslog(LOG_ERR, "Cannot stat: %s", src_path);
            continue;
        }

        // sprawdzamy czy plik istnieje w dst
        if (stat(dst_path, &dst_stat) != 0)
        {
            // plik nie istnieje w dst - trzeba skopiować
            syslog(LOG_INFO, "New file, copying: %s", entry->d_name);
            copy_file(src_path, dst_path, &src_stat, threshold);
        }
        else
        {
            // plik istnieje - porównujemy daty modyfikacji
            if (src_stat.st_mtime > dst_stat.st_mtime)
            {
                syslog(LOG_INFO, "File modified, copying: %s", entry->d_name);
                copy_file(src_path, dst_path, &src_stat, threshold);
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
        // usuwanie nadmiarowych podkatalogow i plików z dst
        if (entry->d_type == DT_DIR) {
            // znowu pomijamy kropki, zeby nie usunac calego systemu
            if (!recursive || strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
            snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);
            // sprawdzamy czy katalog istnieje w src, jesli go tam nie ma to usuwamy z dst
            if (stat(src_path, &src_stat) != 0) {
                syslog(LOG_INFO, "Dir removed from src, deleting: %s", entry->d_name);
                delete_dir_recursively(dst_path);
            }
            continue;
        }

        if (entry->d_type != DT_REG) continue;

        // nie rekurencyjne usuwanie plików
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);

        // sprawdzamy czy plik istnieje w src
        if (stat(src_path, &src_stat) != 0) {
            // plik nie istnieje w src - usuwamy z dst
            syslog(LOG_INFO, "File removed from src, deleting: %s", entry->d_name);
            delete_file(dst_path);
        }
    }

    closedir(dir);
}
void sigusr1_handler(int signum) {
    (void)signum;
    syslog(LOG_INFO, "Odebrano sygnal SIGUSR1 - natychmiastowe wybudzenie demona");
    // Gdy proces odbiera sygnał i wykonuje handler, trwająca funkcja sleep() w pętli głównej zostaje automatycznie przerwana
}

int main(int argc, char *argv[]) {
    // Sprawdzenie liczby argumentów
    if (argc < 3) {
        fprintf(stderr, "Użycie: %s [-R] <src> <dst>\n", argv[0]);
        return 1;
    }

    char *src = NULL;
    char *dst = NULL;
    int recursive = 0;
    unsigned int sleep_time = 300; // domyślnie 5 minut
    off_t threshold = DEFAULT_THRESHOLD;
    int num_count = 0; // licznik opcjonalnych argumentów liczbowych


    // Sprawdzamy, czy użytkownik podał 4 argumenty i czy pierwszy z nich to -R
    // parsujemy argumenty
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-R") == 0) {
            recursive = 1;
        } else if (src == NULL) {
            src = argv[i];
        } else if (dst == NULL) {
            dst = argv[i];
        } else {
            // pierwszy licznik to sleep_time, drugi to threshold
            if (num_count == 0) {
                sleep_time = atoi(argv[i]);
            } else {
                threshold = atoi(argv[i]);
            }
            num_count++;
        }
        i++;
    }

    // Dodatkowe sprawdzenie czy podano obie sciezki
    if (src == NULL || dst == NULL) {
    fprintf(stderr, "Użycie: %s [-R] <src> <dst> [czas]\n", argv[0]);
    return 1;
}

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
    signal(SIGUSR1, sigusr1_handler);

    // Główna pętla
    while (1) {
        syslog(LOG_INFO, "Demon sie obudzil - sprawdzam katalogi");
        sync_dirs(src, dst, recursive, threshold);
        syslog(LOG_INFO, "Demon spi...");
        sleep(sleep_time);
    }

    closelog();
    return 0;
}
