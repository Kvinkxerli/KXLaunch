# KXLaunch

<p align="center">
  <img width="80" height="80" alt="KXLaunch Icon" src="icon.png"/>
</p>

<h3 align="center">Кастомное, быстрое и энергоэффективное Home Menu для Nintendo Switch</h3>

<p align="center">
  <a href="https://t.me/kvinkxerli"><img src="https://img.shields.io/badge/Telegram-@kvinkxerli-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white" alt="Telegram"/></a>
  <a href="https://github.com/Kvinkxerli/kxlaunch--slaunch-/releases"><img src="https://img.shields.io/github/v/release/Kvinkxerli/kxlaunch--slaunch-?style=for-the-badge&color=brightgreen" alt="Releases"/></a>
  <img src="https://img.shields.io/badge/Platform-Nintendo%20Switch-E60012?style=for-the-badge&logo=nintendoswitch&logoColor=white" alt="Nintendo Switch"/>
  <img src="https://img.shields.io/badge/CFW-Atmosph%C3%A8re-informational?style=for-the-badge" alt="Atmosphere"/>
</p>

---

<p align="center">
  <img width="100%" alt="KXLaunch Preview Line" src="https://github.com/user-attachments/assets/fc5fcfc0-1348-454c-a057-2459c590818a" />
</p>
<p align="center">
  <img width="100%" alt="KXLaunch Preview Flow" src="https://github.com/user-attachments/assets/25b4fedb-23fb-4c87-9cf6-0260a3c94ac2" />
</p>

---

## 🌟 Ключевые особенности KXLaunch

* **⚡ Оффлайн-выбор профиля без назойливой привязки к Nintendo:**
  При запуске игры (если на консоли 2 и более аккаунтов) открывается красивое меню выбора пользователя с аватарами и никнеймами. В отличие от стокового апплета Horizon, выбор профиля в KXLaunch **не требует привязки к аккаунту Nintendo** и не блокирует запуск игр на непривязанных или оффлайн-профилях в кастомной прошивке (CFW).
* **🛠️ Исправление вылетов игр и авто-создание сохранений:**
  Решена проблема крашей при первом запуске требовательных игр на движках Unreal Engine / Unity (например, *A Hat in Time* с ошибкой `0x527d4` / `NN_ABORT`). Проект корректно создает обязательные разделы сохранений (`owner_id`, `Thumbnail` только для профильных данных, `None` для системных/временных/кэш-данных).
* **📦 Безупречный запуск Homebrew:**
  Собственный загрузчик `hbloader` с поддержкой запуска в режиме апплета (Album) и полнопамятном режиме приложения (через donor-игру). Сгенерированы и встроены все необходимые дескрипторы `main.npdm`, исключающие вылеты в главное меню при выборе любого `.nro`.
* **🔊 Приятные звуковые эффекты (UI SFX):**
  Полноценное озвучивание интерфейса: перемещение по списку игр (`Move`), листание страниц (`PageLeft`/`PageRight`), выбор и запуск (`Confirm`/`Click`), возврат (`Back`).
* **📅 Полная локализация даты и верхней панели:**
  Дата в верхнем левом углу отображается на чистом русском языке с правильными сокращениями дней недели и месяцев (`12:00 Вс окт 25`).
* **🧹 Очищенное и оптимизированное меню:**
  Убраны лишние и неиспользуемые компоненты (встроенный браузер и виджеты), за счет чего меню работает максимально плавно и потребляет минимум ресурсов аккумулятора.
* **🔄 Встроенная проверка обновлений KXLaunch:**
  Проверка свежих релизов напрямую из репозитория проекта [Kvinkxerli/kxlaunch--slaunch-](https://github.com/Kvinkxerli/kxlaunch--slaunch-).
* **🎨 Богатые возможности кастомизации:**
  8 режимов интерфейса (минималистичный **Line** по умолчанию, **Grid**, **Flow** coverflow, **Deck**, **Shelf**, **Cover**, **List**, **XMB**), поддержка видео-обоев MP4 с аппаратным декодированием NVDEC, наборов иконок и TTF/OTF шрифтов.
* **📁 USB MTP (передача файлов по кабелю):**
  Встроенный быстрый MTP-сервер: подключайте Switch к компьютеру по USB-C и передавайте файлы напрямую на SD-карту прямо из меню.

---

## 🎮 Управление в меню

| Кнопка | Действие |
| :---: | :--- |
| **A** | Запуск выбранной игры / выбор профиля / подтверждение |
| **B** | Назад / отмена |
| **X** | Контекстное меню (избранное, переименование, выбор обложки, порядок) |
| **Y** | Поиск игр / фильтрация |
| **D-Pad / Стик** | Навигация по играм и меню |
| **L / R** | Быстрое перелистывание страниц |
| **ZL / ZR** | Быстрый переход в начало / конец списка |
| **- (Минус)** | Меню быстрых настроек оформления |

---

## 📥 Установка на Nintendo Switch

1. Перейдите на страницу [Релизов](https://github.com/Kvinkxerli/kxlaunch--slaunch-/releases) и скачайте архив **`kxlaunch.zip`**.
2. Распакуйте архив прямо в корень вашей SD-карты (`sdmc:/`), подтвердив замену файлов.
3. Убедитесь, что на SD-карте присутствуют:
   - `sdmc:/atmosphere/contents/0100000000001000/exefs/main` и `main.npdm` (системный демон)
   - `sdmc:/slaunch/bin/sMenu/main` (интерфейс меню)
   - `sdmc:/slaunch/bin/hbloader/main` и `main.npdm` (загрузчик homebrew)
   - `sdmc:/slaunch/lang/ru.txt` (русская локализация)
4. Перезагрузите Nintendo Switch в кастомную прошивку (Atmosphère).

---

## 🛠️ Сборка из исходников

### Необходимые инструменты
* Установленный тулчейн **devkitPro** для Nintendo Switch:
  * `devkitA64`
  * `libnx`
  * `AtmosphereLibs`
* Библиотеки Switch:
  `switch-dev`, `switch-sdl2`, `switch-sdl2_ttf`, `switch-sdl2_image`, `switch-sdl2_mixer`, `switch-curl`, `switch-mbedtls`.

### Команды для сборки
```bash
# Сборка проекта и подготовка структуры SdOut/
make

# Создание архива релиза kxlaunch.zip
make package

# Полная очистка скомпилированных файлов
make clean
```

---

## 💬 Связь с автором и поддержка

Если у вас возникли вопросы, предложения или пожелания по развитию KXLaunch:

* **Telegram разработчика:** [@kvinkxerli](https://t.me/kvinkxerli)
* **GitHub Issues:** [Создать тикет](https://github.com/Kvinkxerli/kxlaunch--slaunch-/issues)

---

## 🙏 Credits & Благодарности

* **sLaunch (etonedemid):** база оригинального проекта.
* **uLaunch (Xortroll & contributors):** архитектура демон-апплет, ECS и NPDM (GPLv2).
* **nx-hbloader (switchbrew):** базовый загрузчик Homebrew (ISC).
* **Atmosphere (haze):** протокол передачи файлов USB MTP (GPLv2).
* **MeepCat55:** дизайн набора иконок `Minimal`.
* **Xelu:** векторные иконки кнопок контроллеров (CC0).
* **averne / FFmpeg (nvtegra) & SwitchWave:** аппаратный декодер NVDEC для видео-обоев (GPLv2+).
