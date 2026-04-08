#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>

void stworz_demona() {
    pid_t pid;

    // Krok 1: pierwszy fork
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE); // błąd
    if (pid > 0) exit(EXIT_SUCCESS); // rodzic kończy

    // Krok 2: nowa sesja
    if (setsid() < 0) exit(EXIT_FAILURE);

    // Krok 3: drugi fork
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    // Krok 4: katalog roboczy
    chdir("/");

    // Krok 5: zamknij stdin/stdout/stderr
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Użycie: %s <src> <dst>\n", argv[0]);
        return 1;
    }

    stworz_demona();

    // Od tego miejsca jesteśmy demonem
    openlog("sync-daemon", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Demon uruchomiony, src=%s dst=%s", argv[1], argv[2]);

    // Główna pętla
    while (1) {
        syslog(LOG_INFO, "Demon śpi...");
        sleep(10); // na razie 10 sekund żeby łatwiej testować
        syslog(LOG_INFO, "Demon się obudził");
    }

    closelog();
    return 0;
}