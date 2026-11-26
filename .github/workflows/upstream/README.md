# Workflow mokaGram

Здесь лежат workflow-файлы, унаследованные от mokaGram и Telegram Desktop. GitHub Actions не запускает файлы из подкаталогов, поэтому в форке они выключены.

Сборка bunnyGram: [../release.yml](../release.yml), в интерфейсе GitHub это **Actions → Сборка и релиз**.

Не переносить обратно без правки: часть файлов рассчитана на раннеры Depot (`depot-ubuntu-latest-16`, `depot-macos-latest`) и на инфраструктуру mokaGram (каналы canary, боты, магазин).
