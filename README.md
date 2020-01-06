# Parathread

Pour compiler le code: 
```
gcc-9 bitmap.c edge-detect.c -O2 -ftree-vectorize -fopt-info -mavx2 -fopt-info-vec-all -lpthread -o apply-effect
```

Pour exécuter:
```
[*exe name*] [*input folder*] [*output folder*] [*thread count*] [*algorithm*]
```

*thread count*: doit être inférieur ou égale au nombre d'images dans *input folder*
*algorithm* : *boxblur*, *edgedetect* ou *sharpen*.

Ex: `./apply-effect ./in/ ./out/ 3 boxblur`
