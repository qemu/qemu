cd x86_64-softmmu
for file in hw/i386/*.hex; do
    cp -f $file ../$file.generated
done
