const $=id=>document.getElementById(id);
let state={stage:0,preferences:false,english:false,light:true,path:'',key:'Print Screen',desktop:true,menu:true,launch:true,version:'0.7.5'};
const send=(action,value='')=>window.chrome?.webview?.postMessage(action+'\n'+value);
const t=(ru,en)=>state.english?en:ru;
function labels(){return {eyebrow:t('УСТАНОВКА','INSTALLATION'),primary:t('Установить','Install'),settingsButton:t('Настройки установки','Installation settings'),settingsTitle:t('Настройки установки','Installation settings'),back:t('Назад','Back'),languageLabel:t('Язык','Language'),themeLabel:t('Тема','Theme'),light:t('Светлая','Light'),dark:t('Тёмная','Dark'),pathLabel:t('Папка установки','Installation folder'),browse:t('Выбрать…','Browse…'),desktopLabel:t('Ярлык на рабочем столе','Desktop shortcut'),menuLabel:t('Ярлык в меню «Пуск»','Start menu shortcut'),accept:t('Принять и установить','Accept and install'),status:t('Установка…','Installing…'),success:t('Установка завершена','Installation complete'),continue:t('Продолжить','Continue'),nextHint:t('Далее — выбор горячей клавиши.','Next, choose your keyboard shortcut.'),hotkeyTitle:t('Горячая клавиша','Keyboard shortcut'),hotkeyHint:t('Нажмите поле, затем клавишу для создания скриншота.','Click the field, then press a key to capture screenshots.'),next:t('Далее','Next'),confirmTitle:t('Заменить системное действие?','Replace the Windows action?'),confirmHint:t('Назначить Print Screen приложению Screenshot вместо стандартного инструмента Windows?','Assign Print Screen to Screenshot instead of the default Windows capture tool?'),cancel:t('Отмена','Cancel'),replace:t('Да, заменить','Yes, replace'),readyTitle:t('Установка завершена','Installation complete'),done:t('Готово','Done'),launchLabel:t('Запустить Screenshot после установки','Launch Screenshot after installation'),version:t('Версия ','Version ')+state.version,retry:t('Повторить','Retry'),noticeTitle:t('Уведомление','Notice')};}
window.update=next=>{
 state={...state,...next};document.documentElement.lang=state.english?'en':'ru';document.documentElement.dataset.theme=state.light?'light':'dark';
 const names=['welcome','installing','installed','hotkey','confirm','finish'];let stage=state.preferences?'settings':names[state.stage];document.body.dataset.stage=stage;
 for(const name of [...names,'settings'])$(name).hidden=name!==stage;
 for(const [id,value] of Object.entries(labels()))if($(id).textContent!==value)$(id).textContent=value;
 $('ru').setAttribute('aria-pressed',String(!state.english));$('en').setAttribute('aria-pressed',String(state.english));$('light').setAttribute('aria-pressed',String(state.light));$('dark').setAttribute('aria-pressed',String(!state.light));
 if(document.activeElement!==$('path')&&$('path').value!==state.path)$('path').value=state.path;
 for(const [id,name]of [['desktop','desktop'],['startMenu','menu'],['launch','launch']])$(id).checked=state[name];
 $('progress').value=state.progress||0;$('keyField').textContent=state.listening?t('Нажмите клавишу…','Press a key…'):state.key;
 $('shortcutInfo').textContent=state.relogin?t('Для активации клавиши перезапустите сеанс Windows.','Sign out and back in to activate the key.'):t('Горячая клавиша: ','Keyboard shortcut: ')+state.key;
 $('saveInfo').textContent=t('PNG → Pictures\\Screenshot + буфер обмена','PNG → Pictures\\Screenshot + clipboard');
 $('notice').hidden=!state.notice;$('noticeText').textContent=state.notice||'';$('failure').hidden=!state.failure;$('error').textContent=state.failure||'';$('close').disabled=state.working;
};
for(const [id,action]of Object.entries({minimize:'minimize',close:'close',settingsButton:'settings',back:'back',primary:'install',accept:'install',continue:'continue',next:'next',replace:'replace',cancel:'cancel',done:'done',browse:'browse',keyField:'listen',noticeOk:'notice',retry:'retry'}))$(id).onclick=()=>{if(action==='install'){send('path',$('path').value);$('path').blur()}send(action)};
for(const id of ['ru','en'])$(id).onclick=()=>send('language',id);
for(const id of ['light','dark'])$(id).onclick=()=>send('theme',id);
for(const [id,action]of [['desktop','desktop'],['startMenu','menu'],['launch','launch']])$(id).onchange=()=>send(action,$(id).checked?'1':'0');
$('path').oninput=()=>send('path',$('path').value);
$('keyField').onkeydown=e=>{if(!state.listening)return;e.preventDefault();send('key',[e.keyCode,(e.ctrlKey?2:0)|(e.shiftKey?4:0)|(e.altKey?1:0)].join(','))};
keyField.onkeyup=e=>{if(state.listening&&e.key==='PrintScreen'){e.preventDefault();send('key','44,0')}};
document.querySelector('header').onpointerdown=e=>{if(!e.target.closest('button'))send('drag')};
document.addEventListener('keydown',e=>{if(e.key==='Escape'){send('escape');e.preventDefault()}});
window.update(state);send('ready');
