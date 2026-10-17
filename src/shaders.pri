# Bakes the GLSL sources into .qsb files at build time and embeds them under :/splat/shaders.
#
# NOTE: The .qsb outputs are listed through a RESOURCES variable (not a hand-written .qrc), so
#       qmake writes the .qrc at qmake time and rcc depends on the exact qsb outputs.
#
# NOTE: Desktop GL needs GLSL 420 because SPIRV-Cross emits packHalf2x16 / unpackHalf2x16.

SPLAT_SHADERS = $$PWD/shaders/accumulate.vert \
                $$PWD/shaders/accumulate.frag \
                $$PWD/shaders/splat.vert \
                $$PWD/shaders/splat.frag

SPLAT_SHADER_INCLUDES = $$PWD/shaders/splatDefines.glsl \
                        $$PWD/shaders/packedSH.glsl

QSB = $$shell_path($$[QT_HOST_BINS]/qsb)

qsb.input    = SPLAT_SHADERS
qsb.output   = $$OUT_PWD/shaders/${QMAKE_FILE_IN_BASE}${QMAKE_FILE_EXT}.qsb
qsb.commands = $$QSB --glsl \"300 es,420\" --hlsl 50 --msl 12 -o ${QMAKE_FILE_OUT} ${QMAKE_FILE_IN}
qsb.depends  = $$SPLAT_SHADER_INCLUDES
qsb.variable_out = SPLAT_QSB
qsb.CONFIG  += no_link target_predeps

QMAKE_EXTRA_COMPILERS += qsb

shaders.prefix = /splat
shaders.base   = $$OUT_PWD

for (shader, SPLAT_SHADERS) {
    shaders.files += $$OUT_PWD/shaders/$$basename(shader).qsb
}

RESOURCES += shaders

OTHER_FILES += $$SPLAT_SHADERS $$SPLAT_SHADER_INCLUDES
