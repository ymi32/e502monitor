# e502monitor

Программа e502monitor предназначена для сбора данных с АЦП L-Card E-502.

Программа формирует бинарные файлы следующего вида:

|<----header---->|<-------------data------------->|

hedaer:


Для того, чтобы перевести эти данный в wav - файлы можно воспользоваться скриптом,
который находится в директории scripts:

./converter.py входной_файл директория_для_аудофайлов

или

./converter.py директория_с_бинарными_файлами директория_для_аудиофайлов
