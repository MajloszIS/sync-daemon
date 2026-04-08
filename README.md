# sync-daemon

Demon synchronizujący dwa katalogi - projekt na Systemy Operacyjne.

## Kompilacja
```bash
make
```

## Użycie
```bash
./daemon src/ dst/
./daemon src/ dst/ 300    # własny czas spania w sekundach
./daemon -R src/ dst/     # rekurencyjna synchronizacja
```

## Autor
MajloszIS
