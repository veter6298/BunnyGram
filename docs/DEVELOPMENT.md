# Устройство репозитория

```text
Telegram/SourceFiles/mokagram/
├── plugins/    движок плагинов, магазин, права
├── msginfo/    окно «Информация о сообщении»
├── music/      плеер
├── streamer/   режим стримера
├── ghost/      призрак
├── commands/   быстрые команды
└── settings/   настройки bunnyGram
patches/        правки поверх upstream
```

Всё добавленное mokaGram лежит в `Telegram/SourceFiles/mokagram/`, правки mokaGram помечены `Mokagram:`, правки bunnyGram помечаются `Bunnygram:`. Каталог и namespace оставлены как в mokaGram, чтобы форк проще подтягивался к upstream; отличия форка описаны в [BUNNYGRAM.md](BUNNYGRAM.md).
