// Русский: text of the main process. Keys as in en.cjs; a key left out reads en.cjs.
const pluralRules = new Intl.PluralRules('ru');
const plural = (n, one, few, many) => {
  const form = pluralRules.select(n);
  return form === 'one' ? one : form === 'many' ? many : few;
};

module.exports = {
  starting: 'Запуск…',

  trayOpenSettings: 'Открыть настройки',
  trayShowPet: 'Показать питомца',
  trayLaunchAtLogin: 'Запускать при входе в систему',
  trayRestart: 'Перезапустить',
  trayQuit: 'Выйти',

  coreFailed: (times, code, logFile) => `Core завершился ${times} ${plural(times, 'раз', 'раза', 'раз')} за 5 минут (код выхода ${code}) и больше не перезапускается. Если приложение только что обновилось, выберите «Выйти» в меню в трее и снова откройте Coopanion. Журнал: ${logFile}`,
  coreRestarting: (code) => `Core неожиданно завершился (код выхода ${code}); перезапуск через 3 секунды`,
  coreLocked: (pid, file) => `Другой процесс${pid ? ` (PID ${pid})` : ''} использует данные Coopanion, поэтому Core не запустился. Дождитесь его завершения или завершите его сами, затем выберите «Перезапустить» в меню в трее. Файл блокировки: ${file}`,

  strandedMessage: 'Найдены настройки, сохранённые до обновления',
  strandedDetail: (stranded, parent) => `Одно из прошлых автоматических обновлений установило Coopanion туда, где он сейчас. Настройки, API Key, промпты и память, сохранённые до этого обновления, по-прежнему находятся здесь:\n${stranded}\n\n`
    + `При возврате текущие данные переименовываются в data-replaced-<время> и остаются в ${parent}; ничего не удаляется. После выбора «Оставить текущие» этот вопрос больше не появится.`,
  strandedRestore: 'Вернуть прежние настройки',
  strandedKeep: 'Оставить текущие',

  dataNotWritable: (dir, suggested) => `Coopanion не может записывать в папку данных и не запустился:\n${dir}\n\nЗапустите установщик снова и установите программу в папку, в которую ваша учётная запись может записывать, например:\n${suggested}`,
};
