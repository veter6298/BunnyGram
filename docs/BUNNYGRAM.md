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

Сабмодули `Telegram/lib_ui` и `Telegram/lib_webview` указывают на форки mokaGram. Для независимого распространения сделайте свои форки этих репозиториев и обновите `.gitmodules`:

```
git submodule set-url Telegram/lib_ui https://github.com/OWNER/lib_ui.git
git submodule set-url Telegram/lib_webview https://github.com/OWNER/lib_webview.git
git submodule sync
git submodule update --init --recursive
```

Клонировать форк нужно с сабмодулями:

```
git clone --recursive https://github.com/OWNER/bunnyGram.git
```

## Обновления

Автообновления отключены: `up.moka.wtf` и ключ подписи принадлежат mokaGram, форк не должен принимать их обновления. Когда появится свой сервер и свой ключ Ed25519:

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

- [ ] заменить `OWNER/bunnyGram` в README, LEGAL, BUNNYGRAM_NOTICE.md и `.github/workflows/release.yml`;
- [ ] задать `maintainer` в nfpm-пакетах;
- [ ] проверить сборку на macOS, Windows и Linux;
- [ ] проверить вход в аккаунт и миграцию данных (каталог данных новый);
- [ ] проверить, что автообновления не ходят на `up.moka.wtf`;
- [ ] решить, остаются ли ссылки на канал и форум mokaGram или заменяются на свои;
- [ ] если нужен свой магазин плагинов и музыкальный бэкенд, поднять их и заменить URL;
- [ ] сохранить `MOKAGRAM_NOTICE.md` и `BUNNYGRAM_NOTICE.md`.

## Юридическое

bunnyGram распространяется под GPLv3. Код mokaGram можно использовать только с сохранением авторства: см. [MOKAGRAM_NOTICE.md](../MOKAGRAM_NOTICE.md) и [BUNNYGRAM_NOTICE.md](../BUNNYGRAM_NOTICE.md). Название и иконка не используют бренд Telegram; bunnyGram не связан с Telegram Messenger LLP.
