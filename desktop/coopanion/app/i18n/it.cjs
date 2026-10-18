// Italiano: text of the main process. Keys as in en.cjs; a key left out reads en.cjs.
module.exports = {
  starting: 'Avvio in corso…',

  trayOpenSettings: 'Apri impostazioni',
  trayShowPet: "Mostra l'animaletto",
  trayLaunchAtLogin: "Avvia all'accesso",
  trayRestart: 'Riavvia',
  trayQuit: 'Esci',

  coreFailed: (times, code, logFile) => `Il Core si è chiuso ${times} volte in 5 minuti (codice di uscita ${code}) e non viene più riavviato. Se l'app è stata appena aggiornata, scegli «Esci» nel menu dell'area di notifica e riapri Coopanion. Log: ${logFile}`,
  coreRestarting: (code) => `Il Core si è chiuso inaspettatamente (codice di uscita ${code}); riavvio tra 3 secondi`,
  coreLocked: (pid, file) => `Un altro processo${pid ? ` (PID ${pid})` : ''} sta usando i dati di Coopanion, quindi il Core non è partito. Attendi che termini o chiudilo, poi scegli «Riavvia» nel menu dell'area di notifica. File di blocco: ${file}`,

  strandedMessage: "Trovate le impostazioni di prima dell'aggiornamento",
  strandedDetail: (stranded, parent) => `Un precedente aggiornamento automatico ha installato Coopanion dove si trova ora. Le impostazioni, le API Key, i prompt e la memoria di prima di quell'aggiornamento sono ancora in:\n${stranded}\n\n`
    + `Tornando indietro, i dati attuali vengono rinominati in data-replaced-<ora> e conservati in ${parent}; non viene eliminato nulla. Dopo «Tieni quelle attuali» la domanda non verrà più posta.`,
  strandedRestore: 'Usa le impostazioni di prima',
  strandedKeep: 'Tieni quelle attuali',

  dataNotWritable: (dir, suggested) => `Coopanion non può scrivere nella sua cartella dei dati e non è partito:\n${dir}\n\nAvvia di nuovo il programma di installazione e installa in una cartella in cui il tuo account può scrivere, per esempio:\n${suggested}`,
};
