savedcmd_char_dev.mod := printf '%s\n'   char_dev.o | awk '!x[$$0]++ { print("./"$$0) }' > char_dev.mod
