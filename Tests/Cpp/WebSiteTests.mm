#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

#include "Engine/Backend/LocalServer.hpp"
#include "Engine/Client/JoinCoordinator.hpp"
#include "Engine/Client/Store.hpp"
#include "Engine/Core/Png.hpp"
#include "Golden.hpp"
#include "Interface/ClientMedia.hpp"
#include "Interface/Components.hpp"
#include "Interface/Tokens.hpp"
#include "ServerHarness.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <mutex>

@interface LDSiteLoad : NSObject <WKNavigationDelegate>
@property(nonatomic) BOOL finished;
@property(nonatomic, strong) NSMutableArray<NSString*>* opened;
@end

@implementation LDSiteLoad
- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
    self.finished = YES;
}
- (void)webView:(WKWebView*)webView decidePolicyForNavigationAction:(WKNavigationAction*)action decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    NSURL* url = action.request.URL;
    if ([url.scheme isEqualToString:@"ludissey"]) {
        if (!self.opened) self.opened = [NSMutableArray array];
        [self.opened addObject:url.absoluteString];
        decisionHandler(WKNavigationActionPolicyCancel);
        return;
    }
    decisionHandler(WKNavigationActionPolicyAllow);
}
@end

namespace {

namespace js {
const char* const headerLayout = R"JS({const bar=document.querySelector('.topnav').getBoundingClientRect(),searchNode=document.querySelector('.search');check(!searchNode||getComputedStyle(searchNode).display==='none'||searchNode.getBoundingClientRect().bottom<=bar.bottom+1,'Ricerca fuori header');check(document.querySelector('main').getBoundingClientRect().top>=bar.bottom,'Contenuto sotto header');})JS";
const char* const prelude = R"JS(window.check=function(v,m){if(!v)throw Error(m)};window.idle=async function(){do{await new Promise(r=>setTimeout(r,15));}while(pendingOperations||pulling);};window.press=async function(selector){const target=document.querySelector(selector);check(target,'Elemento assente: '+selector);target.click();await idle();};window.submit=async function(values){for(const key in values)document.getElementById(key).value=values[key];document.getElementById('auth-form').dispatchEvent(new Event('submit',{bubbles:true,cancelable:true}));await idle();};window.stat=n=>document.querySelector('[data-stat="'+n+'"] b').textContent;window.enroll=async function(v){document.getElementById('register-username').value=v.username;document.getElementById('register-password').value=v.password;document.getElementById('register-confirm').value=v.confirm;document.getElementById('register-form').dispatchEvent(new Event('submit',{bubbles:true,cancelable:true}));await idle();};window.finish=async function(){document.getElementById('profile-form').dispatchEvent(new Event('submit',{bubbles:true,cancelable:true}));await idle();};document.hasFocus=()=>true;)JS";
const char* const signedOut = R"JS(check(document.querySelector('#auth-form'),'Senza sessione il sito non mostra l’accesso');check(!document.querySelector('.topnav'),'Barra visibile senza account');check(document.querySelector('#register-form')&&document.querySelector('#scarica')&&document.querySelector('.loginbar .download'),'Registrazione o Scarica assenti');check(getComputedStyle(document.documentElement).getPropertyValue('--ludissey-accent').trim().toUpperCase()==='#@@accent@@','Token colore errato');check(getComputedStyle(document.documentElement).getPropertyValue('--ludissey-brand').trim().toUpperCase()==='#6730EE','Marchio non viola');check(typeof LudisseyServer==='undefined'&&typeof LudisseyRules.rules==='object'&&typeof handlers==='undefined'&&typeof emulator==='undefined','Il sito espone un backend locale');check(!document.body.textContent.toLowerCase().includes('dimostrativ'),'Testo dimostrativo presente');check(localStorage.length===0,'Il sito scrive in localStorage');)JS";
const char* const overflow = R"JS(check(document.documentElement.scrollWidth<=innerWidth+1,'Overflow @@page@@ a @@width@@ px');)JS";
const char* const refused = R"JS(await enroll({username:'Martina',password:'segreta1',confirm:'diversa'});check(document.getElementById('confirm-error').textContent.includes('non coincidono'),'Conferma password non verificata');check(TestBackend.calls.length===0,'Richiesta inviata con dati non validi');await submit({username:'Martina',password:'segreta1'});check(document.getElementById('auth-error').textContent==='Nome utente o password non corretti','Accesso con account inesistente: '+document.getElementById('auth-error').textContent);check(sessionStorage.getItem('ludissey-sessione')===null,'Sessione salvata dopo un accesso fallito');)JS";
const char* const registered = R"JS(document.querySelector('[data-action=gender][data-id=female]').click();await enroll({username:'Martina',password:'segreta1',confirm:'segreta1'});check(document.querySelector('h1')&&document.querySelector('h1').textContent==='Ciao, Martina!','Home non caricata dopo la registrazione');check(document.getElementById('connection-status').hidden,'Stato della connessione mostrato senza motivo');check(state.gender==='female'&&state.createdAt.length===10,'Profilo non letto dal backend');check(catalog().length===0&&state.friends.length===0&&state.requests.length===0&&state.notifications.length===0&&collection.catalog.length===0&&collection.inventory.length===0,'Un account nuovo mostra dati non provenienti dal backend');check(document.querySelector('main').textContent.includes('Ancora nessuna esperienza qui.'),'Stato vuoto assente');check(typeof sessionStorage.getItem('ludissey-sessione')==='string'&&localStorage.length===0,'Sessione non conservata solo per la scheda');window.meId=TestBackend.accounts.martina.id;)JS";
const char* const seeded = R"JS(const B=TestBackend;B.enroll('dev-ada','Sara');B.enroll('dev-leo','Leo');B.enroll('dev-mira','Pietro');const world=(id,title,genre,size)=>({id,title,blocks:[B.baseplate(id)],genre,serverSize:size});B.invoke('dev-ada','LudisseyPublishWorld',{world:world('@@city@@','Isola Tranquilla','Simulazione',20)});B.invoke('dev-leo','LudisseyPublishWorld',{world:world('@@second@@','Corsa sui Tetti','Corse',12)});B.invoke('dev-mira','LudisseyPublishWorld',{world:world('5a1f0c2e-7d3b-4c1a-8e2f-000000000004','Circuito a blocchi','Corse',8)});B.invoke(meId,'LudisseyPublishWorld',{world:world('5a1f0c2e-7d3b-4c1a-8e2f-000000000003','Torre dei Venti','Avventura',10)});B.invoke(meId,'LudisseySaveWorld',{world:world('5a1f0c2e-7d3b-4c1a-8e2f-000000000006','Giardino delle altezze','Piattaforme',6)});B.emulator.title.LudisseyExperienceMetadata=JSON.stringify({'@@city@@':{description:'Pesca, costruisci la tua capanna e goditi il tramonto.',featured:true,weeklyPlays:240},'@@second@@':{description:'Salta da un tetto all’altro senza cadere in strada.'},'5a1f0c2e-7d3b-4c1a-8e2f-000000000003':{description:'Sali fino in cima prima che cali il sole.'}});await refresh();render();check(catalog().length===5,'Catalogo non letto dal backend: '+catalog().length);check(catalog().find(e=>e.featured).title==='Isola Tranquilla'&&catalog().find(e=>e.featured).description==='Pesca, costruisci la tua capanna e goditi il tramonto.','Metadati dell’esperienza in evidenza non letti');check(document.querySelectorAll('[data-id="shelf.popular"] .tile').length>0&&document.querySelector('[data-action=see-all]'),'Scaffali della home assenti');check(catalog().find(e=>e.id.endsWith('06')).published===false&&catalog().find(e=>e.id.endsWith('06')).tags.includes('mine'),'Bozza personale non distinta');experienceDetails['@@city@@']=await cloud('LudisseyExperience',{id:'@@city@@'});)JS";
const char* const routePage = R"JS(route='@@page@@';query='';render();)JS";
const char* const registry = R"JS(const B=TestBackend,city='@@city@@',second='@@second@@';route='experience/'+city;experienceTab='Server';await refresh();render();check(document.querySelectorAll('.server').length===0&&document.querySelector('.servers .empty span').textContent===RULES.serverList.emptyTitle&&RULES.serverList.emptyTitle==='Nessun server attivo','Senza giocatori il sito non mostra Nessun server attivo come l’app');const crowd=(id,count)=>{for(let i=1;i<=count;i++){const p='folla-'+id.slice(-2)+'-'+i;B.enroll(p,'Folla'+id.slice(-1)+String(i).padStart(2,'0'));const t=B.invoke(p,'LudisseyRequestSession',{experienceId:id,playerProtocol:RULES.playerProtocol});B.invoke(p,'LudisseyOpenSession',{experienceId:id,sessionId:t.sessionId,joinTicket:t.joinTicket});B.invoke(p,'LudisseyReadySession',{experienceId:id,sessionId:t.sessionId});}};crowd(city,27);crowd(second,12);experienceTab='Informazioni';await refresh();render();const labels=[...document.querySelectorAll('.statbar [data-stat]')].map(e=>e.dataset.stat);check(JSON.stringify(labels)===JSON.stringify(['In gioco','Visite','Creata','Aggiornata','Giocatori per server','Genere']),'Statistiche esperienza incomplete: '+labels);check(stat('In gioco')==='27'&&stat('Visite')==='27','Giocatori e visite non letti dal registro: '+stat('In gioco')+' '+stat('Visite'));check(document.querySelector('.side .author').textContent==='di Sara'&&stat('Giocatori per server')==='20'&&stat('Genere')==='Simulazione','Metadati non letti dal backend');experienceTab='Server';renderContent();const rows=[...document.querySelectorAll('.server small')].map(e=>e.textContent);check(JSON.stringify(rows)===JSON.stringify(['20 su 20','7 su 20']),'Lista server non coerente con l’allocatore: '+rows);const full=document.querySelector('.server[data-server="1"] button');check(full.disabled&&full.textContent==='Pieno','Server pieno selezionabile');check(document.querySelector('.here')===null&&document.querySelector('[data-action=leave]')===null,'Il sito simula una sessione di gioco');appOpened=()=>Promise.resolve(false);await press('[data-action=play]');check(dialog.open&&document.getElementById('dialog-title').textContent==='Per giocare ti serve l’app Ludissey'&&document.querySelectorAll('#dialog-actions button')[1].textContent==='Scarica Ludissey','Senza app il sito non spiega come procedere');dialog.close();window.joinable=document.querySelector('.server[data-server="2"] button').dataset.id;await press('.server[data-server="2"] button');dialog.close();await refresh();experienceTab='Informazioni';renderContent();check(stat('Visite')==='27'&&stat('In gioco')==='27','Il sito altera visite o presenze senza entrare nel gioco');route='experience/5a1f0c2e-7d3b-4c1a-8e2f-00000000dead';render();check(![...document.querySelectorAll('body *')].some(e=>[...e.childNodes].some(c=>c.nodeType===3&&c.textContent.trim().startsWith('['))),'Testo con codice tecnico nella pagina');)JS";
const char* const joinable = R"JS(window.joinable)JS";
const char* const navigation = R"JS(const city='@@city@@';const settle=async()=>{for(let i=0;i<40&&location.hash.slice(1)!==route;i++)await new Promise(r=>setTimeout(r,25));await new Promise(r=>setTimeout(r,30));};const pages={'topNavigation.logo':'home','topNavigation.home':'home','topNavigation.experiences':'experiences','topNavigation.avatar':'avatar','topNavigation.friends':'friends','topNavigation.profile':'avatar'};for(const [id,expected] of Object.entries(pages)){location.hash='experience/'+city;await settle();check(route==='experience/'+city&&document.querySelector('.back'),'Esperienza non aperta prima di '+id);document.querySelector('[data-id="'+id+'"]').click();await settle();check(route===expected&&!document.querySelector('.detail'),'Dopo '+id+' la pagina resta '+route);}location.hash='experience/'+city;await settle();document.querySelector('[data-action=account-menu]').click();document.querySelector('[data-id="account.menu"] a[href="#settings"]').click();await settle();check(route==='settings'&&document.querySelector('[data-id="settings.page"]'),'Impostazioni non aperte dal menu');location.hash='home';await settle();document.querySelector('[data-action=see-all][data-id=popular]').click();await settle();check(document.querySelector('.back span').textContent==='Home','Indietro senza la provenienza');document.querySelector('[data-action=experience]').click();await settle();check(document.querySelector('.back span').textContent==='Popolari','Indietro dall’esperienza non torna all’elenco');document.querySelector('.back').click();await settle();check(route==='experiences'&&listing==='popular','Indietro non riapre l’elenco');document.querySelector('.back').click();await settle();check(route==='home'&&!listing,'Indietro non torna alla Home');)JS";
const char* const cursors = R"JS(const city='@@city@@';const settle=async()=>{await new Promise(r=>setTimeout(r,60));};const acting='a[href],button:not(:disabled),[data-action],[data-setting],[data-field],label[for],select:not(:disabled),input:not(:disabled),textarea:not(:disabled),summary';const acts=e=>{const label=e.closest('label');if(label&&label.querySelector('input:not(:disabled)'))return true;return !!e.closest(acting)&&!e.closest(':disabled,[aria-disabled=true]');};const hovered=[];for(const sheet of document.styleSheets){let rules=[];try{rules=[...sheet.cssRules];}catch(e){}for(const rule of rules)if(rule.selectorText)for(const part of rule.selectorText.split(','))if(part.includes(':hover'))hovered.push(part.split(':hover')[0].trim());}const audit=page=>{for(const e of document.querySelectorAll('body *')){if(e.closest('[hidden]'))continue;if(getComputedStyle(e).cursor==='pointer'&&!acts(e))check(false,page+': mano senza azione su '+e.tagName.toLowerCase()+'.'+[...e.classList].join('.'));}for(const base of hovered){let found=[];try{found=[...document.querySelectorAll(base||'*')];}catch(err){continue;}for(const e of found)if(!acts(e))check(false,page+': hover senza azione su '+base);}};const pages=['home','experiences','avatar','friends','settings','experience/'+city];for(const page of pages){location.hash=page;await settle();audit(page);}for(const tab of [1,2,3]){route='settings';settingsTab=tab;render();await settle();audit('impostazioni '+tab);}location.hash='home';await settle();)JS";
const char* const genres = R"JS(route='experiences';listing='popular';render();check(document.querySelectorAll('#genre option').length===RULES.genres.length+1,'Generi non letti dalle regole condivise');{const pick=document.getElementById('genre');pick.value='Corse';pick.dispatchEvent(new Event('change',{bubbles:true}));}check([...document.querySelectorAll('.row.app')].length>0&&[...document.querySelectorAll('.row.app')].every(t=>t.textContent.includes('Corse')),'Filtro genere errato');genre=null;listing=null;route='experiences';render();const search=document.getElementById('search');search.value='Circuito';search.dispatchEvent(new Event('input',{bubbles:true}));check(route==='experiences'&&document.querySelectorAll('.row.app').length===1,'Ricerca errata');query='';listing='mine';render();check(document.querySelectorAll('.row.app').length===2,'Esperienze personali incoerenti');listing='featured';render();check(document.querySelectorAll('.row.app').length===1,'In evidenza non letto dai metadati');)JS";
const char* const avatarEditor = R"JS(const B=TestBackend;route='avatar';avatarTab='colors';render();check(document.querySelector('[data-action=save-avatar]').disabled,'Salva attivo senza modifiche');document.querySelector('[data-field=\'color:torso\'][data-id=E0392B]').click();check(draft.bodyColors.torso==='E0392B'&&state.avatar.bodyColors.torso!=='E0392B','Bozza e avatar salvato non separati');check(document.querySelector('.avatar-preview svg').innerHTML.includes('#E0392B'),'Anteprima non aggiornata');await press('[data-action=save-avatar]');check(dialog.open&&state.avatar.bodyColors.torso==='E0392B','Avatar non salvato');dialog.close();check(B.invoke(meId,'LudisseyAvatar').avatar.bodyColors.torso==='E0392B','Avatar non salvato sul backend condiviso');draft.bodyColors.leftLeg='2FA84F';render();document.querySelector('[data-action=revert-avatar]').click();check(draft.bodyColors.leftLeg===state.avatar.bodyColors.leftLeg,'Annulla modifiche non ripristina');const foreign=(await cloud('LudisseySaveAvatar',{avatar:{bodyColors:{torso:'123456'},displayName:'x',bodyPreset:'robot',equipped:[{itemID:'inesistente',category:'hat'}]}})).avatar;check(foreign.bodyColors.torso===RULES.avatar.standard.bodyColors.torso&&foreign.displayName==='Martina'&&foreign.equipped.length===0&&RULES.avatar.bodyPresets.includes(foreign.bodyPreset),'Validazione avatar diversa dalle regole');await refresh();avatarTab='clothing';avatarCategory='hair';render();check(collection.catalog.length===0&&document.querySelector('.avatar-controls').textContent.includes('Non hai ancora niente in')&&document.querySelector('main').textContent.includes('Il catalogo è vuoto, per ora.'),'Inventario e catalogo vuoti non mostrati');const groups=Object.values(RULES.avatar.groups).flat();check(RULES.avatar.categories.every(c=>groups.includes(c)),'Categorie del catalogo senza scheda');B.emulator.title.LudisseyAvatarCatalog=JSON.stringify([{id:'test-hair',name:'TestHair',category:'hair',creator:'Martina',free:true}]);await refresh();render();await press('[data-action=avatar-acquire][data-id=test-hair]');check(collection.inventory.length===1,'Oggetto gratuito non ottenuto');avatarCategory='hair';render();document.querySelector('[data-action=avatar-item][data-id=test-hair]').click();check(draft.equipped.some(e=>e.itemID==='test-hair'),'Indossa non equipaggia');await press('[data-action=save-avatar]');dialog.close();check(state.avatar.equipped.length===1&&state.avatar.equipped[0].category==='hair','Oggetto indossato non salvato dal backend');document.querySelector('[data-action=avatar-item][data-id=test-hair]').click();await press('[data-action=save-avatar]');dialog.close();check(state.avatar.equipped.length===0,'Togli non salvato');document.querySelector('[data-action=avatar-item][data-id=test-hair]').click();await press('[data-action=save-avatar]');dialog.close();const forged=(await cloud('LudisseySaveAvatar',{avatar:{...state.avatar,equipped:[{itemID:'non-posseduto',category:'hat'}]}})).avatar;check(forged.equipped.length===0,'Oggetto non posseduto accettato');delete B.emulator.title.LudisseyAvatarCatalog;await refresh();check(state.avatar.equipped.length===0&&collection.catalog.length===0,'Un oggetto rimosso dal catalogo resta indossato');)JS";
const char* const friends = R"JS(const B=TestBackend;B.invoke('dev-mira','LudisseyRequestFriend',{id:meId});await refresh();render();check(state.requests.length===1&&state.notifications.length===1&&state.notifications[0].unread,'Richiesta di amicizia non letta dal backend');check(document.querySelector('[data-action=friends] .badge').textContent==='1','Badge amici errato');route='friends';friendsTab=1;render();await press('[data-action=accept]');friendsTab=0;render();check(state.friends.length===1&&state.requests.length===0&&state.friends[0].online===false,'Richiesta non accettata sul backend');check(document.querySelector('[data-action=friends] .badge')===null,'Badge amici non aggiornato');B.emulator.players['dev-mira'].user.LudisseyPresence=String(Date.now()/1000);await refresh();check(state.friends[0].online===true,'Presenza non letta dal backend');render();document.getElementById('friend-name').value='Sara';await press('[data-action=request-friend]');check(dialog.open&&document.getElementById('dialog-body').textContent==='Richiesta inviata!','Richiesta non inviata');dialog.close();check(B.invoke('dev-ada','LudisseyFriends').requests.some(r=>r.id===meId),'La richiesta non raggiunge il destinatario');document.getElementById('friend-name').value='Inesistente99';await press('[data-action=request-friend]');check(dialog.open&&document.getElementById('dialog-body').textContent==='Account non trovato','Giocatore inesistente accettato');dialog.close();document.querySelector('[data-action=remove-friend]').click();check(dialog.open,'Conferma rimozione assente');document.querySelectorAll('#dialog-actions button')[1].click();await idle();check(state.friends.length===0&&B.invoke(meId,'LudisseyFriends').friends.length===0,'Amico non rimosso dal backend');)JS";
const char* const notifications = R"JS(const B=TestBackend;B.invoke('dev-leo','LudisseyRequestFriend',{id:meId});await refresh();render();check(state.notifications.length===1&&state.notifications[0].unread,'Notifica non generata dalla richiesta');await press('[data-action=notifications]');check(notificationsOpen&&state.notifications[0].unread===true,'Aprire il pannello segna le notifiche come lette');await press('[data-action=notifications-read]');check(notificationsOpen&&state.notifications[0].unread===false,'Notifica non segnata come letta');check(JSON.parse(B.emulator.players[meId].user.LudisseyReadNotifications).includes('dev-leo'),'Lettura non salvata sull’account');await press('[data-action=dismiss]');check(state.notifications.length===0&&document.querySelector('.popover.notifications').textContent.includes('Nessuna notifica'),'Stato vuoto notifiche assente');check(state.requests.length===1&&JSON.parse(B.emulator.players[meId].user.LudisseyDismissedNotifications).includes('dev-leo'),'Rimozione non salvata o richiesta persa');notificationsOpen=false;route='settings';settingsTab=3;render();document.querySelector('[data-setting=sounds]').click();await idle();check(state.preferences.sounds===false&&JSON.parse(B.emulator.players[meId].user.LudisseyPreferences).sounds===false,'Preferenza non salvata sull’account');route='settings';settingsTab=1;render();document.getElementById('bio').value='Costruisco mondi';await press('[data-action=save-bio]');check(state.bio==='Costruisco mondi'&&JSON.parse(B.emulator.players[meId].user.LudisseyPreferences).bio==='Costruisco mondi','Descrizione non salvata sull’account');document.querySelector('[data-setting=publicProfile]').click();await idle();check(state.preferences.publicProfile===true&&typeof B.emulator.players[meId].user.LudisseyPublicProfile==='string','Profilo pubblico non salvato');route='settings';settingsTab=0;render();document.getElementById('email').value='audit@example.test';await press('[data-action=password]');check(B.recovery.includes('audit@example.test')&&dialog.open,'Recupero password non richiesto');dialog.close();document.querySelector('[data-action=logout]').click();check(dialog.open,'Conferma logout assente');document.querySelector('#dialog-actions button').click();check(route==='settings'&&linked(),'Logout non annullato');check(localStorage.length===0,'Il sito scrive in localStorage');)JS";
const char* const account = R"JS(const B=TestBackend;route='settings';settingsTab=0;render();document.querySelector('[data-action=logout]').click();document.querySelectorAll('#dialog-actions button')[1].click();check(document.querySelector('#auth-form')&&sessionStorage.getItem('ludissey-sessione')===null&&state.username===''&&catalog().length===0,'Uscita incompleta');B.accounts.senzaprofilo={id:'W-senza',username:'SenzaProfilo',password:'segreta2'};B.emulator.register('W-senza','SenzaProfilo');await submit({username:'SenzaProfilo',password:'segreta2'});check(needsProfile&&document.querySelector('#profile-form button[type=submit]').textContent==='Entra in Ludissey'&&document.getElementById('username')===null,'Profilo incompleto non richiesto');document.querySelector('[data-action=gender][data-id=female]').click();await finish();check(document.querySelector('h1').textContent==='Ciao, SenzaProfilo!'&&state.gender==='female','Profilo non completato');route='settings';settingsTab=0;render();document.querySelector('[data-action=logout]').click();document.querySelectorAll('#dialog-actions button')[1].click();await submit({username:'Martina',password:'segreta1'});check(document.querySelector('h1').textContent==='Ciao, Martina!'&&state.bio==='Costruisco mondi'&&state.preferences.sounds===false,'I dati dell’account non tornano dal backend dopo un nuovo accesso');)JS";
const char* const expired = R"JS(document.getElementById('auth-error')?document.getElementById('auth-error').textContent:'')JS";
const char* const ended = R"JS(check(sessionStorage.getItem('ludissey-sessione')===null&&document.querySelector('#auth-form'),'Sessione scaduta ancora attiva');)JS";
const char* const done = R"JS(window.ludisseyDone===true?'1':'')JS";
const char* const failure = R"JS(window.ludisseyError||'')JS";
}

std::string solidPng(int side, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = red;
        rgba[i + 1] = green;
        rgba[i + 2] = blue;
        rgba[i + 3] = 255;
    }
    auto png = ld::encodePng(rgba, side, side);
    REQUIRE(png);
    return *png;
}

std::string replaced(std::string text, const std::string& marker, const std::string& value) {
    for (auto at = text.find(marker); at != std::string::npos; at = text.find(marker, at + value.size())) text.replace(at, marker.size(), value);
    return text;
}

bool spin(double seconds, const std::function<bool()>& done) {
    auto end = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) return false;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
        ld::fallbackQueue().pump(0.002);
    }
    return true;
}

void wait(double seconds) { spin(seconds, [] { return false; }); }

struct Outcome {
    bool ok = false;
    std::string value;
};

Outcome evaluate(WKWebView* web, const std::string& source) {
    auto outcome = std::make_shared<Outcome>();
    auto finished = std::make_shared<bool>(false);
    [web evaluateJavaScript:[NSString stringWithUTF8String:source.c_str()]
          completionHandler:^(id result, NSError* error) {
              if (error) {
                  NSString* message = error.userInfo[@"WKJavaScriptExceptionMessage"] ?: error.localizedDescription;
                  outcome->value = message.UTF8String;
              } else {
                  outcome->ok = true;
                  outcome->value = result ? [[NSString stringWithFormat:@"%@", result] UTF8String] : "";
              }
              *finished = true;
          }];
    if (!spin(30, [finished] { return *finished; })) outcome->value = "valutazione JavaScript scaduta";
    return *outcome;
}

void run(WKWebView* web, const std::string& source) {
    auto outcome = evaluate(web, source + ";true");
    INFO(outcome.value);
    REQUIRE(outcome.ok);
}

std::string value(WKWebView* web, const std::string& source) {
    auto outcome = evaluate(web, source);
    INFO(outcome.value);
    REQUIRE(outcome.ok);
    return outcome.value;
}

void load(WKWebView* web, LDSiteLoad* delegate, NSURL* url, NSURL* folder) {
    delegate.finished = NO;
    if (folder) [web loadFileURL:url allowingReadAccessToURL:folder];
    else [web loadRequest:[NSURLRequest requestWithURL:url]];
    REQUIRE(spin(30, [&] { return delegate.finished == YES; }));
}

void runAsync(WKWebView* web, const std::string& body) {
    run(web, "window.ludisseyDone=false;window.ludisseyError='';(async()=>{" + body + "})().catch(e=>{window.ludisseyError=String(e&&e.message||e);}).finally(()=>{window.ludisseyDone=true;})");
    REQUIRE(spin(60, [&] { return evaluate(web, js::done).value == "1"; }));
    std::string problem = value(web, js::failure);
    INFO(problem);
    REQUIRE(problem.empty());
}

}

TEST_CASE("Sito: account online", "[web][site]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        std::string shim = "window.ludisseyLanguage='it';var TestBackend=(function(){var handlers={};var currentPlayerId=null;";
        for (const char* name : {"CloudScript/Rules.js", "CloudScript/Avatar.js", "CloudScript/Ludissey.js", "Tests/Emulator/PlayFabEmulator.js", "Tests/Emulator/PlayFabWeb.js"}) shim += golden::readText(golden::root() / name);
        shim += "return {emulator:emulator,accounts:accounts,recovery:recovery,calls:calls,invoke:webInvoke,enroll:webEnroll,baseplate:baseplate};})();";
        WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
        configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
        [configuration.userContentController addUserScript:[[WKUserScript alloc] initWithSource:[NSString stringWithUTF8String:shim.c_str()] injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES]];
        WKWebView* web = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 1180, 800) configuration:configuration];
        NSWindow* window = [[NSWindow alloc] initWithContentRect:web.frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
        window.releasedWhenClosed = NO;
        window.contentView = web;
        [window orderFront:nil];
        LDSiteLoad* delegate = [[LDSiteLoad alloc] init];
        web.navigationDelegate = delegate;
        auto site = std::filesystem::temp_directory_path() / ("Ludissey Sito " + ld::makeUuid());
        std::filesystem::copy(golden::root() / "Web", site, std::filesystem::copy_options::recursive);
        std::filesystem::copy(golden::root() / "Resources/Backgrounds", site / "backgrounds", std::filesystem::copy_options::recursive);
        std::filesystem::copy(golden::root() / "Resources/Faces", site / "faces", std::filesystem::copy_options::recursive);
        NSURL* page = [NSURL fileURLWithPath:[NSString stringWithUTF8String:(site / "index.html").c_str()]];
        NSURL* folder = [NSURL fileURLWithPath:[NSString stringWithUTF8String:site.c_str()]];
        load(web, delegate, page, folder);
        run(web, js::prelude);
        run(web, replaced(js::signedOut, "@@accent@@", std::string(ui::tokens::color::accent)));
        runAsync(web, "const size=await new Promise(r=>{const i=new Image();i.onload=()=>r(i.naturalWidth+'x'+i.naturalHeight);i.onerror=()=>r('errore');i.src='backgrounds/Login.jpg';});if(size!=='3840x2160')throw Error('background della Login non caricato: '+size);if(!getComputedStyle(document.querySelector('.authwrap')).backgroundImage.includes('Login.jpg'))throw Error('background della Login non applicato');");
        auto snap = [&](const std::string& name) {
            const char* shots = std::getenv("LUDISSEY_SHOTS");
            if (!shots) return;
            auto done = std::make_shared<bool>(false);
            std::string target = (std::filesystem::path(shots) / ("sito " + name + ".png")).string();
            [web takeSnapshotWithConfiguration:nil completionHandler:^(NSImage* image, NSError*) {
                if (image) {
                    CGImageRef cg = [image CGImageForProposedRect:nil context:nil hints:nil];
                    NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithCGImage:cg];
                    [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[NSString stringWithUTF8String:target.c_str()] atomically:YES];
                }
                *done = true;
            }];
            spin(10, [done] { return *done; });
        };
        snap("accesso");
        const std::vector<int> widths{375, 420, 421, 600, 768, 860, 920, 959, 960, 1180, 1400};
        auto resize = [&](int width) {
            [window setContentSize:NSMakeSize(width, 800)];
            web.frame = NSMakeRect(0, 0, width, 800);
            [web layoutSubtreeIfNeeded];
            wait(0.12);
        };
        for (int width : widths) {
            resize(width);
            run(web, replaced(replaced(js::overflow, "@@page@@", "accesso"), "@@width@@", std::to_string(width)));
        }
        resize(1180);
        runAsync(web, js::refused);
        runAsync(web, js::registered);
        std::string city = "5a1f0c2e-7d3b-4c1a-8e2f-000000000001", second = "5a1f0c2e-7d3b-4c1a-8e2f-000000000002";
        auto named = [&](const char* source) { return replaced(replaced(source, "@@city@@", city), "@@second@@", second); };
        runAsync(web, named(js::seeded));
        for (int width : widths) {
            resize(width);
            for (const std::string& name : std::vector<std::string>{"home", "experiences", "avatar", "friends", "settings", "experience/" + city}) {
                INFO(name << " a " << width << " px");
                run(web, replaced(js::routePage, "@@page@@", name));
                wait(0.1);
                run(web, replaced(replaced(js::overflow, "@@page@@", name), "@@width@@", std::to_string(width)) + "check(document.querySelectorAll('header').length===1,'Barra duplicata')");
                run(web, js::headerLayout);
            }
        }
        resize(1180);
        for (const std::string& name : std::vector<std::string>{"home", "experiences", "avatar", "friends", "settings", "scarica", "experience/" + city}) {
            run(web, replaced(js::routePage, "@@page@@", name));
            wait(0.4);
            snap(name.rfind("experience/", 0) == 0 ? "dettagli" : name);
        }
        delegate.opened = [NSMutableArray array];
        runAsync(web, named(js::registry));
        runAsync(web, named(js::navigation));
        runAsync(web, named(js::cursors));
        REQUIRE(delegate.opened.count == 2);
        auto played = ld::ExperienceLink::parse(delegate.opened[0].UTF8String);
        REQUIRE(played);
        CHECK(played->valid());
        CHECK(*played == ld::ExperienceLink{ld::ExperienceId(city), true, std::nullopt});
        auto chosen = ld::ExperienceLink::parse(delegate.opened[1].UTF8String);
        REQUIRE(chosen);
        CHECK(chosen->valid());
        CHECK(chosen->play);
        CHECK(chosen->server == std::optional<ld::ServerId>(ld::ServerId(value(web, js::joinable))));
        run(web, js::genres);
        runAsync(web, js::avatarEditor);
        runAsync(web, js::friends);
        runAsync(web, js::notifications);
        runAsync(web, js::account);
        load(web, delegate, page, folder);
        run(web, js::prelude);
        REQUIRE(spin(20, [&] { return evaluate(web, js::expired).value == "La sessione è scaduta: accedi nuovamente"; }));
        run(web, js::ended);
        [window close];
        std::filesystem::remove_all(site);
    }
}

namespace {

namespace images {
const char* const seed = R"JS(const B=TestBackend,E=B.emulator;B.enroll('dev-ada','Sara');window.img=@@images@@;window.ids=@@ids@@;window.uploadImage=pair=>{B.invoke('dev-ada','LudisseyAssetUpload',{assets:[pair[0]]});E.blobs['edev-ada/'+pair[0]]=pair[1];B.invoke('dev-ada','LudisseyAssetFinalize',{assets:[pair[0]]});};Object.values(img).forEach(uploadImage);window.publishImages=(id,title,extra)=>B.invoke('dev-ada','LudisseyPublishWorld',{world:Object.assign({id,title,blocks:[B.baseplate(id)],genre:'Avventura',serverSize:3},extra)});publishImages(ids.both,'Con icona e miniatura',{icon:img.c[0],thumbnail:img.a[0]});publishImages(ids.thumbnail,'Solo miniatura',{thumbnail:img.b[0]});publishImages(ids.icon,'Solo icona',{icon:img.c[0]});publishImages(ids.none,'Senza immagini',{});publishImages(ids.broken,'Riferimento rotto',{thumbnail:img.d[0]});delete E.entities['edev-ada'].files[img.d[0]];const join=(p,id)=>{const t=B.invoke(p,'LudisseyRequestSession',{experienceId:id,playerProtocol:RULES.playerProtocol});B.invoke(p,'LudisseyOpenSession',{experienceId:id,sessionId:t.sessionId,joinTicket:t.joinTicket});B.invoke(p,'LudisseyReadySession',{experienceId:id,sessionId:t.sessionId});};['p1','p2','p3','p4'].forEach((p,i)=>{B.enroll(p,'Giocatore'+(i+1));join(p,ids.both);});B.enroll('p5','Giocatore5');join('p5',ids.thumbnail);window.mediaSettled=async()=>{for(let i=0;i<400;i++){await new Promise(r=>setTimeout(r,25));hydrateMedia();if(mediaPending.size===0&&i>4)break;}await new Promise(r=>setTimeout(r,60));};window.shown=element=>{const i=element&&element.querySelector('.world-preview,.game-icon-image');return i&&i.complete&&i.naturalWidth>0?i:null;};window.noBroken=()=>[...document.querySelectorAll('.cover img,.appicon img')].every(i=>i.complete&&i.naturalWidth>0);window.cover=id=>document.querySelector('.tile[data-id="'+id+'"] .appicon');window.openList=async()=>{route='experiences';scope='all';query='';render();await refresh();render();await mediaSettled();};window.openDetail=async id=>{route='experience/'+id;experienceTab='Informazioni';render();await refresh();render();await mediaSettled();};)JS";
const char* const listed = R"JS(await openList();check(catalog().length===5,'Catalogo incompleto: '+catalog().length);const both=catalog().find(e=>e.id===ids.both);check(both.icon===img.c[0]&&both.thumbnail===img.a[0],'Icona e miniatura non lette dal backend');check(catalog().find(e=>e.id===ids.icon).thumbnail===null&&catalog().find(e=>e.id===ids.thumbnail).icon===null,'Immagine assente trattata come presente');check(shown(cover(ids.both))&&shown(cover(ids.both)).naturalWidth===24&&shown(cover(ids.both)).src==='data:image/png;base64,'+img.c[1],'Icona caricata non mostrata nell’elenco');check(shown(cover(ids.icon))&&shown(cover(ids.icon)).naturalWidth===24,'Icona senza miniatura non mostrata');for(const id of [ids.thumbnail,ids.none,ids.broken])check(!shown(cover(id))&&cover(id).querySelector('.brand'),'Ripiego assente per '+id);check(noBroken(),'Immagine rotta nella pagina');check(localStorage.length===0,'Immagini salvate in localStorage');)JS";
const char* const detailed = R"JS(await openDetail(ids.both);check(shown(document.querySelector('.appicon.detail'))&&shown(document.querySelector('.appicon.detail')).naturalWidth===24,'Icona non mostrata nei dettagli');check(shown(document.querySelector('.cover.large'))&&shown(document.querySelector('.cover.large')).naturalWidth===32,'Miniatura non mostrata nei dettagli');await openDetail(ids.thumbnail);check(!shown(document.querySelector('.appicon.detail'))&&document.querySelector('.appicon.detail .brand'),'Icona mostrata senza icona caricata');check(shown(document.querySelector('.cover.large')).naturalWidth===40,'Miniatura sovrascritta');await openDetail(ids.icon);check(shown(document.querySelector('.appicon.detail')).naturalWidth===24&&!document.querySelector('.cover.large .world-preview'),'Solo icona non distinta dalla miniatura');await openDetail(ids.broken);check(!document.querySelector('.cover.large .world-preview')&&noBroken(),'Riferimento rotto mostra un’immagine rotta');)JS";
const char* const swapped = R"JS(publishImages(ids.both,'Con icona e miniatura',{icon:img.c[0],thumbnail:img.b[0]});await openDetail(ids.both);check(shown(document.querySelector('.cover.large')).naturalWidth===40,'Miniatura sostituita ma il sito mostra ancora la vecchia');uploadImage(img.e);publishImages(ids.both,'Con icona e miniatura',{icon:img.e[0],thumbnail:img.b[0]});await openDetail(ids.both);check(shown(document.querySelector('.appicon.detail')).naturalWidth===28&&shown(document.querySelector('.cover.large')).naturalWidth===40,'Icona sostituita non mostrata o miniatura alterata');publishImages(ids.both,'Con icona e miniatura',{thumbnail:img.b[0]});await openDetail(ids.both);check(!shown(document.querySelector('.appicon.detail'))&&shown(document.querySelector('.cover.large')).naturalWidth===40,'Icona rimossa ancora visibile');publishImages(ids.both,'Con icona e miniatura',{icon:img.e[0]});await openList();check(shown(cover(ids.both)).naturalWidth===28,'Icona nuova non mostrata nell’elenco');await openDetail(ids.both);check(!document.querySelector('.cover.large .world-preview')&&document.querySelector('.cover.large .brand'),'Miniatura rimossa ancora visibile');)JS";
const char* const reloaded = R"JS(await openDetail(ids.both);check(state.username==='Martina','Sessione non ripresa dopo il ricaricamento');check(shown(document.querySelector('.appicon.detail')).naturalWidth===28&&!document.querySelector('.cover.large .world-preview'),'Dopo il ricaricamento icona e miniatura non corrispondono al backend');)JS";
const char* const facts = R"JS((async()=>{const out={};for(const e of catalog().slice().sort((a,b)=>a.id.localeCompare(b.id))){await openDetail(e.id);const stat=n=>document.querySelector('[data-stat="'+n+'"] b').textContent;const row={id:e.id,title:document.querySelector('.detail h1').textContent,author:document.querySelector('.side .author').textContent.replace(/^di /,''),genre:stat('Genere'),serverSize:stat('Giocatori per server'),visits:stat('Visite'),playing:stat('In gioco'),icon:e.icon,thumbnail:e.thumbnail};experienceTab='Server';render();row.servers=[...document.querySelectorAll('.server')].map(s=>s.dataset.server+' '+s.querySelector('small').textContent+' '+(s.querySelector('button').disabled?'pieno':'aperto'));out[e.id]=row;}window.webFacts=JSON.stringify(out);})())JS";
const char* const exported = R"JS(JSON.stringify((E=>({time:E.time,serial:E.serial,players:E.players,title:E.title,groups:E.groups,entities:E.entities,blobs:E.blobs,members:E.members}))(TestBackend.emulator)))JS";
}

}

TEST_CASE("Sito: icona e miniatura dal backend", "[web][site][images][parity]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        golden::registry();
        std::string shim = "window.ludisseyLanguage='it';var TestBackend=(function(){var handlers={};var currentPlayerId=null;";
        for (const char* name : {"CloudScript/Rules.js", "CloudScript/Avatar.js", "CloudScript/Ludissey.js", "Tests/Emulator/PlayFabEmulator.js", "Tests/Emulator/PlayFabWeb.js"}) shim += golden::readText(golden::root() / name);
        shim += "return {emulator:emulator,accounts:accounts,sessions:sessions,recovery:recovery,calls:calls,invoke:webInvoke,enroll:webEnroll,baseplate:baseplate};})();";
        shim += "(function(){var saved=null;try{saved=sessionStorage.getItem('ludissey-test-backend');}catch(e){}if(!saved)return;var v=JSON.parse(saved);Object.assign(TestBackend.emulator,v.emulator);Object.assign(TestBackend.accounts,v.accounts);Object.assign(TestBackend.sessions,v.sessions);})();";
        WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
        configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
        [configuration.userContentController addUserScript:[[WKUserScript alloc] initWithSource:[NSString stringWithUTF8String:shim.c_str()] injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES]];
        WKWebView* web = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 1180, 800) configuration:configuration];
        NSWindow* window = [[NSWindow alloc] initWithContentRect:web.frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
        window.releasedWhenClosed = NO;
        window.contentView = web;
        [window orderFront:nil];
        LDSiteLoad* delegate = [[LDSiteLoad alloc] init];
        web.navigationDelegate = delegate;
        auto site = std::filesystem::temp_directory_path() / ("Ludissey Sito " + ld::makeUuid());
        std::filesystem::copy(golden::root() / "Web", site, std::filesystem::copy_options::recursive);
        std::filesystem::copy(golden::root() / "Resources/Backgrounds", site / "backgrounds", std::filesystem::copy_options::recursive);
        std::filesystem::copy(golden::root() / "Resources/Faces", site / "faces", std::filesystem::copy_options::recursive);
        NSURL* page = [NSURL fileURLWithPath:[NSString stringWithUTF8String:(site / "index.html").c_str()]];
        NSURL* folder = [NSURL fileURLWithPath:[NSString stringWithUTF8String:site.c_str()]];
        load(web, delegate, page, folder);
        run(web, js::prelude);
        runAsync(web, js::registered);

        ld::Json pictures = ld::Json::object();
        const std::vector<std::tuple<const char*, int, std::uint8_t, std::uint8_t, std::uint8_t>> colours{{"a", 32, 220, 40, 40}, {"b", 40, 40, 80, 220}, {"c", 24, 40, 180, 60}, {"d", 16, 200, 200, 40}, {"e", 28, 120, 40, 200}};
        for (const auto& [key, side, r, g, b] : colours) {
            std::string png = solidPng(side, r, g, b);
            pictures.set(key, ld::Json::array({ld::sha256Hex(png) + ".png", ld::base64Encode(png)}));
        }
        ld::Json ids = ld::Json::object({{"both", "5a1f0c2e-7d3b-4c1a-8e2f-0000000000b1"}, {"thumbnail", "5a1f0c2e-7d3b-4c1a-8e2f-0000000000b2"}, {"icon", "5a1f0c2e-7d3b-4c1a-8e2f-0000000000b3"}, {"none", "5a1f0c2e-7d3b-4c1a-8e2f-0000000000b4"}, {"broken", "5a1f0c2e-7d3b-4c1a-8e2f-0000000000b5"}});
        runAsync(web, ::replaced(::replaced(images::seed, "@@images@@", pictures.dump()), "@@ids@@", ids.dump()));
        runAsync(web, images::listed);
        runAsync(web, images::detailed);

        SECTION("parità con l'app") {
            runAsync(web, std::string("await ") + images::facts);
            auto webFacts = ld::Json::parse(value(web, "window.webFacts"));
            REQUIRE(webFacts);
            auto state = ld::Json::parse(value(web, images::exported));
            REQUIRE(state);
            std::string me = value(web, "meId");
            auto world = std::make_shared<harness::ScriptWorld>();
            world->evaluate("Object.assign(emulator, " + state->dump() + ")");
            auto backend = std::make_shared<harness::ScriptBackend>(world, me);
            auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), backend, nullptr, false);
            harness::awaitVoid(store->refreshWorlds());
            std::vector<ld::WorldSummary> discovered;
            ld::Json published = world->invoke(me, "LudisseyDiscover");
            for (const auto& w : published["worlds"].items()) discovered.push_back(ld::WorldSummary::fromJson(w));
            store->setDiscover(discovered);
            REQUIRE(store->experienceCatalog().size() == webFacts->members().size());
            for (const auto& entry : store->experienceCatalog()) {
                INFO(entry.title);
                harness::awaitVoid(store->loadDetails(entry.id));
                REQUIRE(store->details.count(entry.id));
                const auto& details = store->details.at(entry.id);
                const ld::Json& row = (*webFacts)[entry.id];
                REQUIRE(row.isObject());
                CHECK(row["title"] == ld::Json(details.experience.title));
                CHECK(row["title"] == ld::Json(entry.title));
                CHECK(row["author"] == ld::Json(details.experience.author));
                CHECK(row["genre"] == ld::Json(details.experience.genre));
                CHECK(row["serverSize"] == ld::Json(std::to_string(details.experience.serverSize)));
                CHECK(row["visits"] == ld::Json(std::to_string(details.experience.visits)));
                CHECK(row["playing"] == ld::Json(std::to_string(details.players())));
                CHECK(row["icon"] == (entry.icon ? ld::Json(*entry.icon) : ld::Json()));
                CHECK(row["thumbnail"] == (entry.thumbnail ? ld::Json(*entry.thumbnail) : ld::Json()));
                ld::Json servers = ld::Json::array();
                for (const auto& s : details.servers) servers.push(std::to_string(s.sequence) + " " + std::to_string(s.players) + " su " + std::to_string(s.capacity) + " " + (ld::lifecycleName(s.state) == "full" ? "pieno" : "aperto"));
                CHECK(row["servers"] == servers);
            }
            CHECK((*webFacts)[ids["both"].string()]["playing"] == ld::Json("4"));
            CHECK((*webFacts)[ids["both"].string()]["servers"].size() == 2);
        }

        SECTION("sostituzione, rimozione e ricaricamento") {
            runAsync(web, images::swapped);
            run(web, std::string("sessionStorage.setItem('ludissey-test-backend',JSON.stringify({emulator:JSON.parse(") + images::exported + "),accounts:TestBackend.accounts,sessions:TestBackend.sessions}))");
            load(web, delegate, page, folder);
            run(web, js::prelude);
            run(web, "window.ids=" + ids.dump() + ";window.mediaSettled=async()=>{for(let i=0;i<400;i++){await new Promise(r=>setTimeout(r,25));hydrateMedia();if(mediaPending.size===0&&i>4)break;}await new Promise(r=>setTimeout(r,60));};window.shown=element=>{const i=element&&element.querySelector('.world-preview,.game-icon-image');return i&&i.complete&&i.naturalWidth>0?i:null;};window.openDetail=async id=>{route='experience/'+id;experienceTab='Informazioni';render();await refresh();render();await mediaSettled();};");
            REQUIRE(spin(20, [&] { return evaluate(web, "stateReady&&state.username==='Martina'?'1':''").value == "1"; }));
            runAsync(web, images::reloaded);
        }
        [window close];
        std::filesystem::remove_all(site);
    }
}

TEST_CASE("Sito: inglese", "[website][localization]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
        configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
        [configuration.userContentController addUserScript:[[WKUserScript alloc] initWithSource:@"window.ludisseyLanguage='en';" injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES]];
        WKWebView* web = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 1180, 800) configuration:configuration];
        LDSiteLoad* delegate = [[LDSiteLoad alloc] init];
        web.navigationDelegate = delegate;
        auto site = std::filesystem::temp_directory_path() / ("Ludissey Sito " + ld::makeUuid());
        std::filesystem::copy(golden::root() / "Web", site, std::filesystem::copy_options::recursive);
        NSURL* page = [NSURL fileURLWithPath:[NSString stringWithUTF8String:(site / "index.html").c_str()]];
        NSURL* folder = [NSURL fileURLWithPath:[NSString stringWithUTF8String:site.c_str()]];
        load(web, delegate, page, folder);
        CHECK(value(web, "document.documentElement.lang") == "en");
        CHECK(value(web, "document.querySelector('.loginbar').innerText.includes('Sign In')?'1':'0'") == "1");
        CHECK(value(web, "document.querySelector('.authcard').innerText.includes('Accedi')?'1':'0'") == "0");
        CHECK(value(web, "document.querySelector('.skip').textContent") == "Skip to content");
        std::string leftovers = value(web, "(()=>{const keys=new Set(Object.keys(ludisseyEnglish).filter(k=>ludisseyEnglish[k]!==k.split('@@')[0]).map(k=>k.split('@@')[0]));const found=[];const walk=n=>{if(n.nodeType===3){const t=n.textContent.trim();if(keys.has(t))found.push(t);}else{for(const a of ['aria-label','placeholder','title','alt'])if(n.getAttribute&&keys.has((n.getAttribute(a)||'').trim()))found.push(n.getAttribute(a));for(const c of n.childNodes)walk(c);}};walk(document.body);return found.join('|');})()");
        CHECK(leftovers == "");
        CHECK(value(web, "translateMessage('[ServerFull] Il server è pieno')") == "[ServerFull] The server is full");
        CHECK(value(web, "translateMessage('Massimo 200 oggetti per esperienza')") == "At most 200 objects per experience");
        CHECK(value(web, "tr('{0} su {1}',3,20)") == "3 of 20");
        std::filesystem::remove_all(site);
    }
}
