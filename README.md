<p align="center">
  <img src="docs/readme/logo.png" alt="bunnyGram" width="120">
</p>

<h1 align="center">bunnyGram</h1>

<p align="center">
  Форк mokaGram и Telegram Desktop с переработанным дизайном и новыми функциями.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/лицензия-GPLv3-8e8e93?style=for-the-badge&labelColor=282828" alt="GPLv3"></a>
  <a href="MOKAGRAM_NOTICE.md"><img src="https://img.shields.io/badge/основа-MOKAGRAM-D9A7FF?style=for-the-badge&labelColor=282828" alt="mokaGram"></a>
</p>

## О проекте

**bunnyGram** — форк [mokaGram](https://github.com/mokagram/mokagram), который, в свою очередь, основан на [Telegram Desktop](https://github.com/telegramdesktop/tdesktop). Визуал, MD3-настройки, плагины, музыка, приватность и остальные функции наследуются от mokaGram; bunnyGram меняет имя, оформление, иконки и правила распространения.

Клиент работает с обычным аккаунтом Telegram и обычными серверами. Плагины из экстеры, но на ПК: свой API, Python runtime и магазин прямо в клиенте.

## Что умеет

- **Приватность**: режим стримера прячет номер, юзернейм и ID, режим призрака не отмечает прочитанным и скрывает, что ты в сети
- **Удалённые и правки** сообщений
- **Безопасность**
- **Музыка**: SoundCloud и треки из Telegram, плейлисты, синхронизация лайков, текст песни и комментарии с таймкодами
- **Плеер**: обычная панель или Dynamic Island, на macOS отдельный островок у верхнего края экрана
- **Быстрые команды**
- **Система**: анти-лаг и экономия в фоне
- **Кастомизация** интерфейса
- **Инструменты**: скриншот сообщений карточкой
- **Плагины** в формате `.plugin`, Python runtime и магазин прямо в клиенте

Для воспроизведения недоступных треков SoundCloud из YouTube Music нужен актуальный локальный `yt-dlp`. Скачать его и проверить требования к среде JavaScript можно на [странице yt-dlp](https://github.com/yt-dlp/yt-dlp#dependencies).

Всё лежит в одном месте: **Настройки → Настройки bunnyGram**. Подробно со скриншотами: [docs/FEATURES.md](docs/FEATURES.md).

## Сборка

Готовых сборок пока нет: это исходники форка. Собирается так же, как Telegram Desktop:

- [macOS](docs/building-mac.md)
- [Windows](docs/building-win.md)
- [GNU/Linux (Docker)](docs/building-linux.md)

Устройство репозитория и отличия форка: [docs/BUNNYGRAM.md](docs/BUNNYGRAM.md). Исходники Telegram Desktop и список сторонних библиотек: [telegramdesktop/tdesktop](https://github.com/telegramdesktop/tdesktop).


## Что уже переименовано

- имя приложения, заголовки окон, «О программе», настройки и переводы;
- каталог данных (`~/.local/share/bunnyGram` на Linux) и короткое имя интеграции;
- имя macOS-бандла `bunnyGram` и идентификатор `app.bunnygram.desktop`;
- Linux-пакеты и desktop-файл, Windows exe, артефакты CI;
- ссылка «журнал изменений» в «О программе»;
- иконки и логотип приложения.

Внутренние имена (`Telegram/SourceFiles/mokagram/`, namespace `Mokagram`, ключи настроек, формат `.mokaset`, сервер обновлений) оставлены как в mokaGram, чтобы не ломать совместимость и сборку. Подробности и план дальнейшего переименования: [docs/BUNNYGRAM.md](docs/BUNNYGRAM.md).

Автообновления отключены: манифест mokaGram подписан чужим ключом, форк не должен ставить такие обновления.

## Плагины

Плагин: один файл `.plugin` с классом, наследующим `BasePlugin`. Метаданные, права и команды лежат в самом файле, лимит 300 действий в минуту, каждое действие пишется в журнал. Плагины выполняются на Python runtime и работают с клиентом через API mokaGram.

Полное описание: [docs-plugins-api.md](docs-plugins-api.md), пример: [examples/showcase.plugin](examples/showcase.plugin).

## Авторство и лицензия

> **Основано на mokaGram, автор [@ecronx](https://t.me/ecronx) (https://t.me/ecronx). https://github.com/mokagram/mokagram**

bunnyGram основан на mokaGram, а mokaGram — на [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) (© The Telegram Desktop Authors, GPLv3 с исключением OpenSSL). Доработки mokaGram © 2026 [@ecronx](https://t.me/ecronx), лицензия та же, GPLv3, плюс условие о сохранении авторства.

Кто берёт исходники или сборки, обязан сохранить [уведомление mokaGram](MOKAGRAM_NOTICE.md), [уведомление bunnyGram](BUNNYGRAM_NOTICE.md) и указать авторство в README и в окне «О программе». При нарушении лицензия прекращается.

*Anyone using mokaGram source code **must credit the author** ([@ecronx](https://t.me/ecronx), https://github.com/mokagram/mokagram) in the README and the About window and keep the [notice](MOKAGRAM_NOTICE.md). The bunnyGram fork keeps this attribution and adds [BUNNYGRAM_NOTICE.md](BUNNYGRAM_NOTICE.md).*

bunnyGram не связан с Telegram Messenger LLP. «Telegram» — товарный знак соответствующего правообладателя; название и иконка форка его не используют.
