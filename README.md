# J.A.R.V.I.S. Desktop

Самостоятельное настольное приложение для Windows 10/11 x64 с обычным окном и разделами «Обзор», «Внешний вид», «Поведение» и «О программе».

## Возможности
- Панель состояния системы: CPU, RAM и свободное место на диске C:.
- Настройки прозрачности, размера и положения окна.
- Переключатели анимации, реакции на курсор, ARC Reactor и декоративной графики.
- Сохранение параметров в jarvis.ini.
- Опциональный запуск вместе с Windows.

## Скачать готовую программу
После успешной сборки откройте вкладку Actions репозитория, выберите последний успешный запуск Build Windows app и скачайте артефакт JARVIS-Desktop-Windows-x64.

## Сборка вручную
Нужны CMake и Visual Studio 2022 с компонентом Desktop development with C++:
```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```
Готовый файл: build/Release/JarvisDesktop.exe.

Исходный Windhawk-мод сохранён отдельно в src/JARVIS_Cinematic_HUD.wh.cpp.
