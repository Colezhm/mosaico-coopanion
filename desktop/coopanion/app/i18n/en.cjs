// Text of the main process: the tray menu, the settings window's starting page, the notices when the
// Core exits, the dialog that takes back the data an update left behind, and the error box when the data
// directory cannot be written. Keys as in zh.cjs.
module.exports = {
  /** The line the settings window shows until the Core is up. */
  starting: 'Starting…',

  trayOpenSettings: 'Open settings',
  trayShowPet: 'Show pet',
  trayLaunchAtLogin: 'Start at login',
  trayRestart: 'Restart',
  trayQuit: 'Quit',

  /** The Core exited too often within 5 minutes and is not restarted; shown as a notification and in an error box. */
  coreFailed: (times, code, logFile) => `The Core exited ${times} times within 5 minutes (exit code ${code}) and is no longer restarted. If the app was just updated, choose Quit in the tray menu and open Coopanion again. Log: ${logFile}`,
  coreRestarting: (code) => `The Core exited unexpectedly (exit code ${code}); restarting in 3 seconds`,
  /** Another process holds the data directory's instance lock, so the Core did not start; shown as a notification and in an error box. */
  coreLocked: (pid, file) => `Another process${pid ? ` (PID ${pid})` : ''} is using Coopanion's data, so the Core did not start. Wait for it to exit or end it, then choose Restart in the tray menu. Lock file: ${file}`,

  /** Asked once when an update moved the program and left the old data directory behind. */
  strandedMessage: 'Settings from before the update were found',
  strandedDetail: (stranded, parent) => `An earlier automatic update installed Coopanion where it is now. The settings, API keys, prompts and memory from before that update are still in:\n${stranded}\n\n`
    + `Switching back renames the current data to data-replaced-<time> and keeps it in ${parent}; nothing is deleted. After "Keep the current ones" you will not be asked again.`,
  strandedRestore: 'Use the settings from before',
  strandedKeep: 'Keep the current ones',

  /** The data directory cannot be written (an install in a directory that needs administrator rights), so the app does not start; shown in an error box. */
  dataNotWritable: (dir, suggested) => `Coopanion cannot write to its data directory and did not start:\n${dir}\n\nRun the installer again and install into a directory your user account can write to, for example:\n${suggested}`,
};
