alias wrom='minipro -p 28C256 -uP -w'
#!/bin/bash

rm -f *.bin

for src in $(ls *.s); do
   dst=$(echo ${src} | sed 's/\.s//')
  ${HOME}/.local/bin/vbcc/bin/vasm6502_oldstyle -Fbin -dotdir -wdc02 -o ${dst}.bin ${src}
done
