#!/bin/bash
source Scripts/util.sh || source util.sh

TARGETPATH=Build
TARGETAPP=ImageVis3D.app
PREFIX="${TARGETPATH}/${TARGETAPP}"

if test -n "$1" -a -d "$1" ; then
    echo "Using '$1' as .app directory."
    PREFIX="$1"
fi

if ! test -d "${PREFIX}" ; then
    die "$PREFIX does not exist, build the application first!"
fi

echo "Copying Shaders ..."
rm -f "${PREFIX}/Contents/Resources/*.glsl"
mkdir -p "${PREFIX}/Contents/Resources"
cp tuvok/Shaders/* "${PREFIX}/Contents/Resources"

echo "Removing subversion garbage ..."
find "${PREFIX}" -iname .svn -exec rm -fr {} +
echo "done!"

macdeployqt="macdeployqt"
if test -n "${QT_BIN}" -a -x "${QT_BIN}/macdeployqt" ; then
    macdeployqt="${QT_BIN}/macdeployqt"
fi
echo "Running Qt's mac deployment tool."
${macdeployqt} ${PREFIX}

version
revision

echo "Copying ImageVis3D Manual into app..."
man=$(manual)
pushd ${PREFIX}/Contents/Resources
  rm -f ImageVis3D.pdf
  curl -skLO "${man}"
  mv $(basename "${man}") ImageVis3D.pdf
popd

tarball=$(nm_tarball)
zipfile=$(nm_zipfile)
pushd Build/ &>/dev/null
    ver="${IV3D_MAJOR}.${IV3D_MINOR}.${IV3D_PATCH}"
    sed -i -e "s,VERSION,${ver}," \
      ImageVis3D.app/Contents/Info.plist
    tar zcf ${tarball} ImageVis3D.app
    zip -9r ${zipfile} ImageVis3D.app
popd &>/dev/null
mv Build/${tarball} Build/${zipfile} .
mkdir -p staging
cp -R CmdLineConverter/Build/uvfconvert.app staging/
cp -R Build/ImageVis3D.app staging/
hdiutil create                    \
  -volname "ImageVis3D"           \
  -srcfolder staging/             \
  -format UDZO                    \
  -imagekey zlib-level=9          \
  ${zipfile%%zip}dmg
rm -fr staging/
hdiutil internet-enable -yes ${zipfile%%zip}dmg
