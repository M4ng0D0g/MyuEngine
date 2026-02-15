# ---------------------------------------------------------------------------
# CPackOptions.cmake – loaded by cpack at PACK time via CPACK_PROJECT_CONFIG_FILE.
#
# This file goes through only ONE round of CMake string parsing (by cpack),
# so we can write NSIS syntax almost literally:
#   \\   → \   (single backslash for NSIS paths / registry keys)
#   \"   → "   (quote for NSIS strings)
#   $VAR        (NSIS variable – CMake leaves bare $ alone)
#   $\\r / $\\n (NSIS escape for CR / LF)
# ---------------------------------------------------------------------------

if(CPACK_GENERATOR MATCHES "NSIS")

    # ── Maintenance mode ────────────────────────────────────────────────
    # Detect a previous install, offer Repair / Uninstall / Cancel.
    set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS "
ReadRegStr $0 HKCU Software\\MyuEngine InstallDir
StrCmp $0 \"\" done_maint
MessageBox MB_YESNOCANCEL|MB_ICONQUESTION \"MyuEngine is already installed at $0.$\\r$\\nYes = Repair / Reinstall$\\nNo = Uninstall$\\nCancel = Abort\" IDYES do_repair IDNO do_uninstall
Abort
do_uninstall:
  ExecWait '\"$0\\uninstall.exe\" /S'
  Goto done_maint
do_repair:
  StrCpy $INSTDIR $0
done_maint:
")

    # ── Registry bookkeeping ────────────────────────────────────────────
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
WriteRegStr HKCU Software\\MyuEngine InstallDir $INSTDIR
WriteRegStr HKCU Software\\MyuEngine Version ${CPACK_PACKAGE_VERSION}
")

    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
DeleteRegKey HKCU Software\\MyuEngine
")

endif()
