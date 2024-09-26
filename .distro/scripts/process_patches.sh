SPECNAME=$1
shift
LOCALVERSION=$1
shift

SOURCES=rpmbuild/SOURCES
SPEC=rpmbuild/SPECS/${SPECNAME}

# Pre-cleaning
rm -rf psection

cp ${SPECNAME} ${SPEC}

# Remove the old patch section
sed -i '/^Patch[0-9]*:/d' "${SPEC}"

# Generate the new patch section
num=0
shopt -s nullglob
for patchfile in ${SOURCES}/*.patch; do
  ignore=$(grep -x "Ignore-patch: .*" "$patchfile" | sed 's/Ignore-patch: \(.*\)/\1/')
  [ "$ignore" == "True" ] && rm "$patchfile" && continue
  patchname=$(basename "$patchfile")
  let num=num+1
  echo "Patch${num}: ${patchname}" >> psection
done

# Find the last line of the source section
lp=$(grep -e "Source[0-9]\+:.*" ${SPEC} | tail -n 1)

# Add the new patch section after the source section
sed -i "/$lp/r psection" ${SPEC}

# Append the local version (if any) to the release number
if [ -n "${LOCALVERSION}" ]; then
  sed -i "s/\(^Release:.*\)/\1\.${LOCALVERSION}/" ${SPEC}
fi

# Post-cleaning
rm -rf psection
