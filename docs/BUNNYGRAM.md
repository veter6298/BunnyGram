# bunnyGram: устройство форка

bunnyGram это форк [mokaGram](https://github.com/mokagram/mokagram), основанного на [Telegram Desktop](https://github.com/telegramdesktop/tdesktop). Здесь описано, что уже переименовано, что оставлено как в mokaGram и что нужно доделать.

## Что переименовано

| Что | Было | Стало |
|---|---|---|
| Отображаемое имя | mokaGram | bunnyGram |
| Имя приложения (`QApplication`) | TelegramDesktop | bunnyGram |
| Каталог данных (Linux) | `~/.local/share/TelegramDesktop` | `~/.local/share/bunnyGram` |
| macOS-бандл | mokaGram.app | bunnyGram.app |
| macOS bundle id | com.tdesktop.Telegram | app.bunnygram.desktop |
| Linux-пакеты | mokagram | bunnygram |
| Windows exe | mokaGram.exe | bunnyGram.exe |
| Короткое имя интеграции | tdesktop | bunnygram |

Иконки и логотип приложения заменены на bunnyGram.

## Иконки

Все размеры собираются одним скриптом без внешних зависимостей:

```
python3 tools/bunnygram/make_icons.py
```

Он перерисовывает `Telegram/Resources/art/icon*.png`, `icon256.ico`, `logo_256*.png`, `icon_round512@2x.png`, наборы `Telegram/Telegram/Images.xcassets` и `docs/readme/logo.png`. Цвета и форму зайца можно менять в начале скрипта.

## Что оставлено как в mokaGram

Это сделано намеренно, чтобы не ломать сборку, формат данных и совместимость с плагинами:

- каталог `Telegram/SourceFiles/mokagram/` и namespace `Mokagram`;
- ключи настроек и формат `.mokaset`, файлы плагинов `.plugin`;
- внутренние env-переменные (`MOKAGRAM_DEBUG_CMD`, `MOKAGRAM_UPDATE_KEY` и другие);
- URL mokaGram: канал `t.me/mokagram`, форум `t.me/mokaforum`, магазин плагинов и музыкальные интеграции;
- `AppId` установщика Windows, `AppFile` и имя цели сборки `Telegram`;
- идентификатор приложения Linux `org.telegram.desktop` (D-Bus, autostart, portal).

Переименование каждого пункта выше это отдельная задача, после которой нужна сборка и проверка на всех трёх платформах.

## Сборка

Порядок тот же, что у Telegram Desktop и mokaGram:

- [macOS](building-mac.md)
- [Windows](building-win.md)
- [GNU/Linux (Docker)](building-linux.md)

### GitHub Actions

Один workflow, `.github/workflows/release.yml`, собирает выбранные платформы. Запуск: **Actions → Сборка и релиз → Run workflow**. Тег можно оставить пустым, тогда будут только артефакты; с тегом workflow создаст или обновит релиз.

Секреты `API_ID` и `API_HASH` не обязательны: без них используется тестовая пара Telegram, а вход в аккаунт ограничен. Для нормальной сборки добавьте свои ключи из [my.telegram.org](https://my.telegram.org) в **Settings → Secrets and variables → Actions**.

macOS собирается на arm64-раннере. macOS и Windows не подписаны сертификатом разработчика, поэтому при первом запуске система предупредит.

Остальные workflow mokaGram лежат в `.github/workflows/upstream/` и не запускаются: они рассчитаны на раннеры Depot и инфраструктуру mokaGram.

Сабмодули `Telegram/lib_ui` и `Telegram/lib_webview` указывают на форки mokaGram. Для независимого распространения сделайте свои форки этих репозиториев и обновите `.gitmodules`:

```
git submodule set-url Telegram/lib_ui https://github.com/veter6298/lib_ui.git
git submodule set-url Telegram/lib_webview https://github.com/veter6298/lib_webview.git
git submodule sync
git submodule update --init --recursive
```

Клонировать форк нужно с сабмодулями:

```
git clone --recursive https://github.com/veter6298/bunnyGram.git
```

## Бейджи

Клиент читает список бейджей и поток изменений с сервера bunnyGram:

- `https://144-31-34-156.sslip.io:8090/v1/badges`: список;
- `https://144-31-34-156.sslip.io:8090/v1/events`: SSE, обновления в реальном времени.

Сервер сливает публичный список `api.moka.wtf` с локальными выдачами, поэтому бейдж видят все пользователи клиента, а не только автор выдачи. Если сервер недоступен, клиент берёт тот же список у бота через inline-запрос.

Выдача и снятие через бота `@bodfsoofasdbot` (только админ):

```
/admin
/grant <id> [уровень 1-3]
/revoke <id>
/blue <id> - выдать синюю галочку
/unblue <id> - убрать синюю галочку
/badge <id> <emoji_id> <caption> - свой значок (премиум эмодзи + надпись)
/list
```

Сервер и бот запускаются службами `bunnygram-badges` и `bunnygram-bot`. Сертификат для `144-31-34-156.sslip.io` обновляет certbot.

## Обновления

Автообновления отключены в двух местах: `Core::SetUpdaterDisabledAtStartup()` в `core/launcher.cpp` выключает штатный апдейтер Telegram Desktop, а `kUpdatesEnabled = false` в `Telegram/SourceFiles/mokagram/update/mokagram_update.cpp` выключает апдейтер mokaGram. Причина одна: оба канала обновляют не этот форк.

Когда появится свой сервер и свой ключ Ed25519:

1. поднимите свой каталог с `update.json` и пакетами;
2. замените `kBaseUrl` и `kPublicKey` в `Telegram/SourceFiles/mokagram/update/mokagram_update.cpp`;
3. включите `kUpdatesEnabled`;
4. перепишите `tools/bunnygram/publish_update.sh` под свой сервер.

## Как подтягивать изменения mokaGram

```
git remote add upstream https://github.com/mokagram/mokagram.git
git fetch upstream mokagram
git merge upstream/mokagram
```

Конфликты обычно будут в пользовательских строках, `about_box.cpp`, `version.h`, `CMakeLists.txt` и workflow-файлах. Внутренние файлы mokaGram при этом не переименовываются, поэтому конфликтов в коде меньше.

## Чеклист перед публикацией

- [ ] обновить скриншоты в `docs/readme` и `docs/FEATURES.md` после первой сборки;
- [ ] проверить сборку на macOS, Windows и Linux;
- [ ] проверить вход в аккаунт и миграцию данных (каталог данных новый);
- [ ] проверить, что автообновления не ходят на `up.moka.wtf` и на серверы Telegram;
- [ ] решить, остаются ли ссылки на канал и форум mokaGram или заменяются на свои;
- [ ] если нужен свой магазин плагинов и музыкальный бэкенд, поднять их и заменить URL;
- [ ] сохранить `MOKAGRAM_NOTICE.md` и `BUNNYGRAM_NOTICE.md`.

## Юридическое

bunnyGram распространяется под GPLv3. Код mokaGram можно использовать только с сохранением авторства: см. [MOKAGRAM_NOTICE.md](../MOKAGRAM_NOTICE.md) и [BUNNYGRAM_NOTICE.md](../BUNNYGRAM_NOTICE.md). Название и иконка не используют бренд Telegram; bunnyGram не связан с Telegram Messenger LLP.
