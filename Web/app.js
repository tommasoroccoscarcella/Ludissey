'use strict';
const LANGUAGE = (() => {
    let wanted = window.ludisseyLanguage || '';
    try {
        wanted = wanted || new URLSearchParams(location.search).get('lang') || '';
    } catch (e) {
    }
    wanted = wanted || (navigator.languages && navigator.languages[0]) || navigator.language || 'en';
    return String(wanted).toLowerCase().startsWith('it') ? 'it' : 'en';
})();
function tr(key, ...args) {
    const source = String(key).split('@@')[0], english = typeof ludisseyEnglish === 'object' ? ludisseyEnglish[key] : undefined;
    const text = LANGUAGE === 'en' && typeof english === 'string' ? english : source;
    return text.replace(/\{(\d)\}/g, (m, i) => args[i] !== undefined ? String(args[i]) : m);
}
function translateMessage(message) {
    const text = String(message || '');
    if (LANGUAGE === 'it') return text;
    const coded = /^\[[^\]]+\] /.exec(text), prefix = coded ? coded[0] : '', body = text.slice(prefix.length);
    if (typeof ludisseyEnglish !== 'object') return text;
    if (typeof ludisseyEnglish[body] === 'string') return prefix + ludisseyEnglish[body];
    for (const [key, value] of Object.entries(ludisseyEnglish)) {
        if (!key.includes('{')) continue;
        const order = [];
        const pattern = new RegExp('^' + key.replace(/[.*+?^$()|[\]\\]/g, '\\$&').replace(/\{(\d)\}/g, (m, i) => {
            order.push(Number(i));
            return '(.+?)';
        }) + '$');
        const found = pattern.exec(body);
        if (found) {
            const args = [];
            order.forEach((n, i) => {
                args[n] = found[i + 1];
            });
            return prefix + value.replace(/\{(\d)\}/g, (m, i) => args[i] !== undefined ? args[i] : m);
        }
    }
    return text;
}
document.documentElement.lang = LANGUAGE;
for (const skip of document.querySelectorAll('.skip')) skip.textContent = tr('Vai al contenuto');
const routes = {home: tr('Home'), experiences: tr('Esperienze'), avatar: tr('Avatar'), friends: tr('Amici'), settings: tr('Impostazioni')};
const primary = ['home', 'experiences', 'avatar', 'friends'];
const RULES = LudisseyRules.rules, genres = RULES.genres, ACCOUNT = RULES.account;
document.documentElement.style.setProperty('--skin', '#' + RULES.avatar.standard.bodyColors.head);
const AV = RULES.avatar;
const avatarTabs = {clothing: tr('Abbigliamento'), body: tr('Corpo'), colors: tr('Colori'), animations: tr('Animazioni')};
const categoryTitle = c => tr(AV.categoryTitles[AV.categories.indexOf(c)] || c);
const partTitle = p => tr(AV.bodyPartTitles[AV.bodyParts.indexOf(p)] || p);
function standardAvatar(name, body) {
    return {displayName: name, bodyPreset: AV.bodyPresets.includes(body) ? body : AV.standard.bodyPreset, bodyColors:
        {...AV.standard.bodyColors}, equipped: []};
}
const blank = () =>
    ({username: '', createdAt: '', gender: 'male', bio: '', avatar: standardAvatar('', 'male'), friends: [], requests: [],
        notifications: [], preferences: {sounds: true, friendNotifications: true, publicProfile: false}});
let state = blank();
let draft = JSON.parse(JSON.stringify(state.avatar));
let avatarTab = 'clothing';
let avatarCategory = 'hat';
let collection = {catalog: [], inventory: []};
let refusal = '';
let route = location.hash.slice(1) || 'home';
let query = '';
let scope = 'featured';
let genre = null;
let experienceTab = 'Informazioni';
let notificationsOpen = false;
let register = false;
let gender = 'male';
let needsProfile = false;
let authMessage = '';
let registerMessage = '';
let listing = null;
let trail = [];
let keepTrail = false;
let keepQuery = false;
let friendsTab = 0;
let settingsTab = 0;
let accountMenu = false;
let menuOpen = false;
let searchOpen = false;
let loginOpen = false;
let recovering = false;
let versions = null;
const app = document.getElementById('app'), dialog = document.getElementById('dialog');
let lastFocus = null;
const escapeHTML = value =>
    String(value).replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
const ticketKey = 'ludissey-sessione';
const remembered = key => {
    try {
        return sessionStorage.getItem(key);
    } catch (e) {
        return null;
    }
};
const remember = (key, value) => {
    try {
        if (value === null) sessionStorage.removeItem(key);
        else sessionStorage.setItem(key, value);
    } catch (e) {
    }
};
let ticket = remembered(ticketKey), account = null, onlineEncoded = '', onlineRevision = 0;
let stateRevision = - 1;
let stateReady = false;
let pulling = false;
let pendingOperations = 0;
let statusMessage = '';
let pollTimer = null;
let catalogEntries = [], experienceDetails = {}, stateDirty = false;
const media = new Map(), mediaPending = new Set(), mediaRetries = new Map();
const imageAsset = value => typeof value === 'string' && /^[a-f0-9]{64}\.[a-z0-9]{1,5}$/.test(value) &&
    RULES.assets.imageExtensions.includes(value.slice(65));
const images = w => ({icon: imageAsset(w.icon) ? w.icon : null, thumbnail: imageAsset(w.thumbnail) ? w.thumbnail : null});
const linked = () => !!ticket;
const serviceMessages = {InvalidUsernameOrPassword: tr('Nome utente o password non corretti'), AccountNotFound:
    tr('Account non trovato'), UsernameNotAvailable: tr('Nome utente già utilizzato'), InvalidPassword:
    tr('La password deve contenere da 6 a 100 caratteri'), InvalidUsername: tr('Il nome utente deve contenere da 3 a 20 caratteri'),
    AccountBanned: tr('Questo account è sospeso'), NotAuthenticated: tr('La sessione è scaduta: accedi nuovamente'),
    InvalidSessionTicket: tr('La sessione è scaduta: accedi nuovamente'), APIRequestsDisabledForTitle:
    tr('Servizio temporaneamente non disponibile')};
const expiredCodes = ['InvalidSessionTicket', 'NotAuthenticated', 'SessionTicketExpired', 'AccountBanned', 'AccountDeleted'];
function fail(reply) {
    const message = String(reply.error), coded = message.startsWith('[') && message.includes(']');
    const failure = new Error(translateMessage(coded ? message.slice(message.indexOf(']') + 1).trim() : message));
    failure.code = coded ? message.slice(1, message.indexOf(']')) : '';
    return failure;
}
function endSession(message) {
    ticket = null;
    account = null;
    remember(ticketKey, null);
    clearInterval(pollTimer);
    pollTimer = null;
    state = blank();
    draft = JSON.parse(JSON.stringify(state.avatar));
    collection = {catalog: [], inventory: []};
    catalogEntries = [];
    experienceDetails = {};
    stateReady = false;
    stateRevision = - 1;
    onlineEncoded = '';
    needsProfile = false;
    register = false;
    authMessage = message || '';
}
async function playfab(method, body, authenticated = true) {
    if (authenticated && !ticket) throw Error(tr('Accedi per continuare'));
    const controller = new AbortController(), timeout = setTimeout(() => controller.abort(), 25000);
    try {
        const response = await fetch('https://' + ACCOUNT.titleId + '.playfabapi.com/Client/' + method,
            {method: 'POST', headers: authenticated ? {'Content-Type' : 'application/json', 'X-Authorization': ticket}:
                {'Content-Type': 'application/json'}, body: JSON.stringify(body || {}), signal: controller.signal, cache: 'no-store'});
        const reply = await response.json();
        if (!response.ok || reply.code !== 200) {
            const failure = Error(serviceMessages[reply.error] || reply.errorMessage || tr('Impossibile completare la richiesta'));
            failure.code = reply.error || '';
            if (authenticated && expiredCodes.includes(failure.code)) {
                endSession(failure.message);
                render();
            }
            throw failure;
        }
        return reply.data && typeof reply.data === 'object' ? reply.data : {};
    } catch (error) {
        if (error.name === 'AbortError') throw Error(tr('Il server non risponde. Riprova tra poco.'));
        throw error;
    } finally {
        clearTimeout(timeout);
    }
}
async function cloudScript(name, args) {
    const data = await playfab('ExecuteCloudScript',
        {FunctionName: name, FunctionParameter: args || {}, RevisionSelection: 'Live', GeneratePlayStreamEvent: false});
    if (data.Error) {
        const logged = (data.Logs || []).map(l => l.Data && l.Data.Error).find(e => typeof e === 'string');
        throw fail({error: logged || data.Error.Message || tr('Operazione non completata')});
    }
    if (!data.FunctionResult || typeof data.FunctionResult !== 'object') throw Error(tr('Risposta incompleta dal server'));
    return data.FunctionResult;
}
function composeCatalog(published, own, metadata) {
    const entries = {}, known = id => metadata[id] && typeof metadata[id] === 'object' ? metadata[id] : {};
    const describe = (id, author) => typeof known(id).description === 'string' ? known(id).description :
        tr('Un’esperienza di {0}.', author);
    const plays = id => typeof known(id).weeklyPlays === 'number' ? known(id).weeklyPlays : null;
    for (const w of published) {
        const featured = known(w.id).featured === true;
        entries[w.id] = {id: w.id, title: w.title, description: describe(w.id, w.author), author: w.author, genre: w.genre ||
            RULES.defaultGenre, serverSize: w.serverSize || RULES.serverSize.standard, createdAt: w.createdAt || null, updatedAt:
            w.updatedAt || null, featured, weeklyPlays: plays(w.id), ...images(w), published: true, tags:
            ['all', 'community'].concat(featured ? ['featured'] : [])};
    }
    for (const w of own) {
        const entry = entries[w.id] ||
            {id: w.id, title: w.title, description: describe(w.id, w.author), author: w.author, genre: w.genre || RULES.defaultGenre,
                serverSize: w.serverSize || RULES.serverSize.standard, createdAt: w.createdAt || null, updatedAt: w.updatedAt || null,
                featured: false, weeklyPlays: plays(w.id), ...images(w), published: false, tags: ['all']};
        entry.tags = entry.tags.concat('mine');
        entries[w.id] = entry;
    }
    return Object.values(entries).sort((a, b) => a.title.localeCompare(b.title, 'it'));
}
async function onlineState() {
    const boot = await cloudScript('LudisseyBootstrap', {});
    if (boot.needsProfile || !boot.profile) {
        const failure = Error(tr('Completa il tuo profilo'));
        failure.code = 'NeedsProfile';
        throw failure;
    }
    const info = await cloudScript('LudisseyServerInfo', {});
    if (info.schemaVersion !== RULES.schemaVersion || info.playerProtocol !== RULES.playerProtocol) {
        const failure = Error(tr('C’è una nuova versione del sito. Ricarica la pagina.'));
        failure.code = 'VersionMismatch';
        throw failure;
    }
    const [data, friendReply, discover, own, title,
        avatarReply] = await Promise.all([playfab('GetUserData',
            {Keys: ['LudisseyPreferences', 'LudisseyDismissedNotifications', 'LudisseyReadNotifications', 'LudisseyPublicProfile']}),
            cloudScript('LudisseyFriends', {}), cloudScript('LudisseyDiscover', {}), cloudScript('LudisseyWorlds', {}),
            playfab('GetTitleData', {Keys: ['LudisseyExperienceMetadata']}), cloudScript('LudisseyAvatar', {})]);
    const values = data.Data || {}, decoded = key => {
        try {
            return values[key] && typeof values[key].Value === 'string' ? JSON.parse(values[key].Value) : null;
        } catch (e) {
            return null;
        }
    };
    const saved = decoded('LudisseyPreferences');
    const valid = !!saved && typeof saved.sounds === 'boolean' && typeof saved.friendNotifications === 'boolean' &&
        typeof saved.publicProfile === 'boolean' && typeof saved.bio === 'string';
    const preferences = {sounds: valid ? saved.sounds : true, friendNotifications: valid ? saved.friendNotifications : true,
        publicProfile: 'LudisseyPublicProfile' in values, bio: valid ? saved.bio : ''};
    const requests = (friendReply.requests || []).filter(r => typeof r.id === 'string' && typeof r.username === 'string').map(r =>
        ({id: r.id, name: r.username, at: typeof r.at === 'string' ? r.at : ''}));
    const active = new Set(requests.map(r => r.id)), names = key => {
        const list = decoded(key);
        return new Set((Array.isArray(list) ? list : []).filter(n => typeof n === 'string' && active.has(n)));
    };
    const dismissed = names('LudisseyDismissedNotifications'), read = names('LudisseyReadNotifications');
    const friends = await Promise.all((friendReply.friends || []).filter(f => typeof f.FriendPlayFabId === 'string').map(async f => {
        let stamp = 0;
        try {
            const presence = await playfab('GetUserData', {PlayFabId: f.FriendPlayFabId, Keys: ['LudisseyPresence']});
            stamp = Number(presence.Data && presence.Data.LudisseyPresence && presence.Data.LudisseyPresence.Value) || 0;
        } catch (e) {
        }
        const age = Date.now()/1000 - stamp;
        return {id: f.FriendPlayFabId, name: f.Username || f.TitleDisplayName || f.FriendPlayFabId, online: age >= 0 &&
            age < ACCOUNT.presenceSeconds};
    }));
    let pictures = {};
    try {
        if (friends.length) pictures = (await cloudScript('LudisseyPortraits', {players: friends.map(f => f.id)})).players || {};
    } catch (e) {
    }
    for (const f of friends) {
        const shown = pictures[f.id];
        f.portrait = shown ? shown.bust : null;
        f.portraitUrl = shown ? shown.bustUrl : null;
        f.gender = shown && shown.gender === 'female' ? 'female' : 'male';
    }
    let metadata = {};
    try {
        const raw = title.Data && title.Data.LudisseyExperienceMetadata;
        if (typeof raw === 'string') {
            const parsed = JSON.parse(raw);
            if (parsed && typeof parsed === 'object') metadata = parsed;
        }
    } catch (e) {
    }
    const notifications = preferences.friendNotifications ?
        requests.filter(r => !dismissed.has(r.id)).map(r =>
            ({id: r.id, title: tr('Richiesta di amicizia'), body: tr('{0} vuole aggiungerti agli amici.', r.name), unread:
                !read.has(r.id), at: r.at})):
        [];
    account = {profile: boot.profile, preferences, dismissed, read};
    return {username: boot.profile.username, gender: boot.profile.gender, createdAt: String(boot.profile.createdAt || '').slice(0, 10),
        bio: preferences.bio, preferences:
        {sounds: preferences.sounds, friendNotifications: preferences.friendNotifications, publicProfile: preferences.publicProfile},
        friends, requests, notifications, avatar: avatarReply.avatar || null, collection:
        {catalog: avatarReply.catalog || [], inventory: avatarReply.inventory || []}, catalog:
        composeCatalog(discover.worlds || [], own.worlds || [], metadata)};
}
async function savePreferences(next) {
    if (next.publicProfile) await playfab('UpdateUserData',
        {Data: {LudisseyPublicProfile: JSON.stringify(account.profile)}, Permission: 'Public'});
    else await playfab('UpdateUserData', {KeysToRemove: ['LudisseyPublicProfile']});
    await playfab('UpdateUserData', {Data: {LudisseyPreferences: JSON.stringify(next)}, Permission: 'Private'});
    return {};
}
async function loadState() {
    const snapshot = await onlineState(), encoded = JSON.stringify(snapshot);
    if (encoded !== onlineEncoded) {
        onlineEncoded = encoded;
        onlineRevision++;
    }
    return {...snapshot, revision: onlineRevision};
}
function signedAccount() {
    if (!account) throw Error(tr('Account non ancora caricato'));
    return account;
}
function saveBio(bio) {
    return savePreferences({...signedAccount().preferences, bio: Array.from(String(bio || '')).slice(0, ACCOUNT.bioCharacters).join('')});
}
function savePreference(key, value) {
    if (!['sounds', 'friendNotifications', 'publicProfile'].includes(key) || typeof value !== 'boolean') throw Error(tr('Preferenza non valida'));
    return savePreferences({...signedAccount().preferences, [key]: value});
}
async function sendFriendRequest(username) {
    signedAccount();
    const name = String(username || '').trim();
    if (name.length < 3 || name.length > 20) throw Error(tr('Nome utente: 3–20 caratteri.'));
    const info = await playfab('GetAccountInfo', {Username: name});
    if (!info.AccountInfo || typeof info.AccountInfo.PlayFabId !== 'string') throw Error(tr('Giocatore non trovato'));
    await cloudScript('LudisseyRequestFriend', {id: info.AccountInfo.PlayFabId});
}
async function recoverPassword(email) {
    const address = String(email || '');
    if (!address.includes('@') || address.length > 254) throw Error(tr('Inserisci un indirizzo email valido'));
    await playfab('SendAccountRecoveryEmail', {TitleId: ACCOUNT.titleId, Email: address}, false);
}
async function markNotifications(dismiss) {
    const owner = signedAccount();
    if (dismiss) owner.dismissed.add(String(dismiss));
    else state.notifications.forEach(n => owner.read.add(n.id));
    await playfab('UpdateUserData',
        {Data:
            {LudisseyDismissedNotifications: JSON.stringify([...owner.dismissed].sort()), LudisseyReadNotifications:
                JSON.stringify([...owner.read].sort())},
            Permission: 'Private'});
}
function openInApp(experience, server) {
    location.href = 'ludissey://experience/' + encodeURIComponent(experience) + '/play' + (server ? '?server=' + encodeURIComponent(server) : '');
}
async function cloud(name, args) {
    return cloudScript(name, args || {});
}
async function pull() {
    const incoming = await loadState(), dirty = JSON.stringify(draft) !== JSON.stringify(state.avatar);
    const changed = incoming.revision !== stateRevision;
    stateRevision = incoming.revision;
    catalogEntries = incoming.catalog || [];
    state = {...state, username: incoming.username, gender: incoming.gender, createdAt: incoming.createdAt, bio: incoming.bio,
        preferences: incoming.preferences, friends: incoming.friends, requests: incoming.requests, notifications:
        incoming.notifications || [], avatar: incoming.avatar || standardAvatar(incoming.username, incoming.gender)};
    collection = incoming.collection || {catalog: [], inventory: []};
    if (!stateReady || !dirty) draft = JSON.parse(JSON.stringify(state.avatar));
    stateReady = true;
    return changed;
}
async function refresh() {
    if (!linked() || pulling || pendingOperations) return;
    pulling = true;
    try {
        const changed = await pull();
        statusMessage = '';
        const selected = route.startsWith('experience/') ? route.slice(11) : null;
        if (selected) experienceDetails[selected] = await cloud('LudisseyExperience', {id: selected});
        const focused = document.activeElement, editing = focused && ['INPUT', 'TEXTAREA', 'SELECT'].includes(focused.tagName);
        stateDirty = stateDirty || changed;
        if (!editing && !dialog.open) {
            if (stateDirty) {
                render();
                stateDirty = false;
            } else if (selected) renderContent();
            else updateConnection();
        }
    } catch (error) {
        if (!linked()) {
            render();
            return;
        }
        if (error.code === 'NeedsProfile') {
            needsProfile = true;
            render();
            return;
        }
        statusMessage = error.code === 'VersionMismatch' ? error.message : tr('Connessione persa. {0}', error.message);
        updateConnection();
    } finally {
        pulling = false;
    }
}
function updateConnection() {
    const status = document.getElementById('connection-status');
    if (status) {
        status.textContent = statusMessage;
        status.classList.toggle('error', !!statusMessage);
        status.hidden = !status.textContent;
    }
    if (!stateReady) render();
}
async function operation(work) {
    if (pendingOperations) return;
    pendingOperations++;
    try {
        await work();
        if (linked()) {
            await pull();
            render();
        }
    } catch (error) {
        if (linked()) {
            const text = String(error.message), cut = text.indexOf('. ');
            const split = cut > 0 && cut + 2 < text.length;
            showDialog(split ? text.slice(0, cut) : tr('Operazione non riuscita'), split ? text.slice(cut + 2) : text,
                [{label: tr('Chiudi')}, {label: tr('Riprova'), run: () => refresh()}], true);
        }
    } finally {
        pendingOperations--;
    }
}
function startPolling() {
    clearInterval(pollTimer);
    pollTimer = setInterval(refresh, 30000);
}
async function authenticate(name, password) {
    if (pendingOperations) return;
    pendingOperations++;
    authMessage = '';
    registerMessage = '';
    try {
        if (!needsProfile) {
            const data = {TitleId: ACCOUNT.titleId, Username: name, Password: password};
            if (register) data.RequireBothUsernameAndEmail = false;
            const reply = await playfab(register ? 'RegisterPlayFabUser' : 'LoginWithPlayFab', data, false);
            if (typeof reply.SessionTicket !== 'string') throw Error(tr('Sessione non ricevuta'));
            ticket = reply.SessionTicket;
            remember(ticketKey, ticket);
        }
        const boot = await cloudScript('LudisseyBootstrap', register || needsProfile ? {gender} : {});
        if (boot.needsProfile) {
            needsProfile = true;
            return;
        }
        needsProfile = false;
        await pull();
        route = 'home';
        startPolling();
    } catch (error) {
        if (register || needsProfile) registerMessage = error.message;
        else authMessage = error.message;
    } finally {
        pendingOperations--;
        render();
    }
}
function mediaKey(element) {
    const asset = element.dataset.asset;
    return asset ? 'asset:' + asset : 'scene:' + element.dataset.media + ':' + (element.dataset.revision || '');
}
async function mediaSource(element) {
    const id = element.dataset.media, asset = element.dataset.asset;
    if (!asset) return null;
    const urls = (await cloud('LudisseyAssetResolve', {experienceId: id, assets: [asset]})).urls || {};
    return typeof urls[asset] === 'string' ? urls[asset] : null;
}
function showMedia(element, key, source) {
    const picture = new Image();
    picture.alt = '';
    picture.className = element.dataset.kind === 'icon' ? 'game-icon-image' : 'world-preview';
    picture.addEventListener('error', () => {
        const retries = (mediaRetries.get(key) || 0) + 1;
        mediaRetries.set(key, retries);
        if (retries < 2) media.delete(key);
        else media.set(key, null);
        render();
    });
    picture.src = source;
    element.replaceChildren(picture);
    element.classList.add('loaded');
}
function hydrateMedia() {
    if (!linked() || !stateReady) return;
    hydratePortraits();
    document.querySelectorAll('[data-media]').forEach(element => {
        const key = mediaKey(element);
        if (media.has(key)) {
            const source = media.get(key);
            if (source && !element.classList.contains('loaded')) showMedia(element, key, source);
            return;
        }
        if (mediaPending.has(key) || !element.dataset.asset) return;
        mediaPending.add(key);
        mediaSource(element).then(source => {
            if (source === undefined) {
                mediaPending.delete(key);
                return;
            }
            if (!source) {
                media.set(key, null);
                mediaPending.delete(key);
                return;
            }
            const probe = new Image();
            probe.onload = () => {
                media.set(key, source);
                mediaPending.delete(key);
                document.querySelectorAll('[data-media]').forEach(node => {
                    if (mediaKey(node) === key) showMedia(node, key, source);
                });
            };
            probe.onerror = () => {
                media.set(key, null);
                mediaPending.delete(key);
            };
            probe.src = source;
        }).catch (() => {
            media.set(key, null);
            mediaPending.delete(key);
        });
    });
}
const portraitSources = new Map();
function hydratePortraits() {
    document.querySelectorAll('[data-portrait]').forEach(node => {
        const name = node.dataset.portrait;
        const friend = state.friends.find(f => f.name === name);
        const key = name + '|' + ((friend && friend.portrait) || '');
        if (portraitSources.has(key)) {
            const source = portraitSources.get(key);
            const shown = node.querySelector('img');
            if (source && (!shown || shown.getAttribute('src') !== source)) node.innerHTML = '<img alt="" src="' + source + '">';
            return;
        }
        portraitSources.set(key, (friend && friend.portraitUrl) || null);
        if (portraitSources.get(key)) hydratePortraits();
    });
}
function catalog() {
    return catalogEntries;
}
const matching = name => name.toLocaleLowerCase('it').includes(query.trim().toLocaleLowerCase('it'));
const icon = name => typeof ludisseyGlyphs !== 'undefined' && ludisseyGlyphs[name] ?
    ludisseyGlyphs[name].replace('<svg ', '<svg class="icon" aria-hidden="true" fill="currentColor" '): '';
const brand = () => '';
function searchPrompt() {
    return {home: tr('Cerca esperienze'), experiences: tr('Cerca esperienze'), friends: tr('Cerca amici'), avatar:
        tr('Cerca oggetti')}[route] ||
        (route.startsWith('experience/') ? tr('Cerca esperienze') : '');
}
function standardBust(name) {
    const friend = state.friends.find(f => f.name === name);
    const gender = name === state.username ? state.gender : friend ? friend.gender : 'male';
    return `<img alt="" src="portraits/${gender === 'female' ? 'Female' : 'Male'}.png">`;
}
function bust(name, kind) {
    return `<span class="bust ${kind}" data-portrait="${escapeHTML(name)}" aria-hidden="true">${standardBust(name)}</span>`;
}
function presence(online) {
    return `<span class="dot${online ? ' online' : ''}"></span>`;
}
function nav(id) {
    const title = routes[id], selected = route === id || (id === 'experiences' && route.startsWith('experience/'));
    const count = id === 'friends' && state.requests.length ? `<span class="badge">${state.requests.length}</span>` : '';
    return `<a class="navlink" href="#${id}" data-id="topNavigation.${id}" ${id === 'friends' ? 'data-action="friends"' : ''} ` +
        `${selected ? 'aria-current="page"' : ''}><span class="face"><span>${escapeHTML(title)}</span>${count}</span></a>`;
}
function pack(name, points) {
    const file = typeof ludisseyIcons === 'object' ? ludisseyIcons[name] : '';
    if (!file) return '';
    return `<img class="pack" src="icons/${file}-${points}.png" srcset="icons/${file}-${points * 2}.png 2x" ` +
        `width="${points}" height="${points}" alt="">`;
}
function empty(sentence, symbol = 'ld-icon-app-experiences', action = '') {
    return `<div class="group empty">${pack(symbol, 32)}<span>${escapeHTML(sentence)}</span><span class="grow"></span>${action}</div>`;
}
function tabs(values, current, action, small = false) {
    return `<div class="seg${small ? ' small' : ''}" role="tablist">` + Object.entries(values).map(([key, title]) =>
        `<button role="tab" aria-selected="${String(current) === key}" data-action="${action}" data-id="${key}">` +
        `${escapeHTML(title)}</button>`).join('') + '</div>';
}
function hash(id) {
    let value = 2166136261;
    for (const c of id) value = Math.imul(value^c.charCodeAt(0), 16777619);
    return (value>>>0) % 6;
}
const genreTint = name => '#' + RULES.part.palette[[3, 8, 11, 0, 21, 22, 4, 9][(genres.indexOf(name) + 1) % 8]];
function coverMarkup(entry, large) {
    return (`<span class="cover` + (large ? ' large' : '') + `" ` + (large ? 'data-id="experience.cover"' : '') + ` data-media="` +
        (escapeHTML(entry.id)) + `" data-kind="thumbnail" data-asset="` + (entry.thumbnail || '') + `" data-revision="` +
        (escapeHTML(entry.updatedAt || '')) + `" style="--tint:` + (genreTint(entry.genre)) + `">` + (brand()) + `</span>`);
}
function meta(entry) {
    const details = experienceDetails[entry.id], playing = details ? details.servers.reduce((n, s) => n + s.players, 0) : 0;
    return !entry.published ? tr('Bozza') : playing > 0 ? tr('{0} in gioco', playing) : tr(entry.genre);
}
function appIcon(entry, kind) {
    const asset = entry.icon || '';
    return `<span class="appicon ${kind}" data-media="${escapeHTML(entry.id)}" data-kind="icon" data-asset="${asset}" ` +
        `style="--tint:${genreTint(entry.genre)}">${brand()}</span>`;
}
function playing(entry) {
    const details = experienceDetails[entry.id];
    return details ? details.servers.reduce((n, s) => n + s.players, 0) : 0;
}
function tile(entry) {
    return `<button class="tile" data-action="experience" data-id="${entry.id}" title="${escapeHTML(entry.title)}">` +
        appIcon(entry, 'shelf') + `<span class="info"><strong>${escapeHTML(entry.title)}</strong>` +
        `<small>${escapeHTML(meta(entry))}</small></span></button>`;
}
function row(entry) {
    const count = playing(entry), byline = entry.author ? tr('di {0}', entry.author) + ' · ' + tr(entry.genre) : tr(entry.genre);
    return `<div class="row app"><button class="open" data-action="experience" data-id="${entry.id}" title="` +
        `${escapeHTML(entry.title)}">${appIcon(entry, 'list')}<span class="words"><b>${escapeHTML(entry.title)}</b>` +
        `<small>${escapeHTML(byline)}</small>${count ? `<em>${escapeHTML(tr('{0} in gioco', count))}</em>` : ''}</span></button>` +
        `<span class="grow"></span><button class="green small" data-action="play" data-id="${entry.id}">${tr('Gioca')}</button></div>`;
}
function list(entries) {
    if (!entries.length) return empty(tr('Ancora nessuna esperienza qui.'));
    const split = window.innerWidth >= token('breakpoint-small') && entries.length > 1, half = split ? Math.ceil(entries.length / 2) : entries.length;
    const column = part => `<div class="group">${part.map(row).join('')}</div>`;
    return `<div class="rows">${column(entries.slice(0, half))}${split ? column(entries.slice(half)) : ''}</div>`;
}
const shelfTitles = {featured: tr('In evidenza'), popular: tr('Popolari'), recent: tr('Aggiornate di recente'), community:
    tr('Dalla community'), mine: tr('Le tue esperienze')};
function shelfEntries(kind) {
    const all = catalog().filter(e => !genre || e.genre === genre);
    if (kind === 'featured') return all.filter(e => e.featured);
    if (kind === 'popular') return all.filter(e => e.published).sort((a, b) => (b.weeklyPlays || 0) - (a.weeklyPlays || 0));
    if (kind === 'recent') return all.filter(e => e.published && e.updatedAt).sort((a, b) => b.updatedAt.localeCompare(a.updatedAt));
    if (kind === 'community') return all.filter(e => e.tags.includes('community'));
    return all.filter(e => e.tags.includes('mine'));
}
function grid(entries) {
    return list(entries);
}
const token = name => parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--ludissey-' + name)) || 0;
function perRow() {
    const width = window.innerWidth, gap = token('space-s8');
    const side = width >= token('breakpoint-wide') ? token('size-tile-standard') : token('size-tile-compact');
    const room = Math.min(width - 2 * token('space-s12'), token('size-content'));
    return Math.max(1, Math.floor((room + gap) / (side + gap)));
}
function shelf(kind) {
    const entries = shelfEntries(kind);
    const all = entries.length ? `<button class="purple small" data-action="see-all" data-id="${kind}">${tr('Vedi tutte')}</button>` : '';
    const studio = kind === 'mine' ? `<a class="button white" href="#scarica">${tr('Apri Ludissey Studio')}</a>` : '';
    const none = kind === 'mine' ? empty(tr('Non hai ancora creato niente.'), 'ld-icon-app-create', studio) :
        empty(tr('Ancora nessuna esperienza qui.'));
    return `<section class="shelf" data-id="shelf.${kind}"><div class="shelfhead"><h2>${shelfTitles[kind]}</h2>${all}</div>` +
        (entries.length ? `<div class="shelfrow">${entries.slice(0, perRow()).map(tile).join('')}</div>` : none) + '</section>';
}
function results() {
    const found = catalog().filter(e => matching(e.title));
    return `<h2>${escapeHTML(tr('Risultati per «{0}»',query))}</h2>` +
        (found.length ? grid(found) : empty(tr('Nessun risultato per «{0}».', query), 'ld-icon-app-search'));
}
function home() {
    const online = state.friends.filter(f => f.online).length;
    const line = online ? `<small>${escapeHTML(online === 1 ? tr('{0} amico è online adesso', online) :
        tr('{0} amici sono online adesso', online))}</small>` : '';
    const head = `<div class="hello"><span class="headshot" data-id="home.headshot">${bust(state.username, 'size-headshot')}</span>` +
        `<span class="greeting"><h1>${escapeHTML(tr('Ciao, {0}!', state.username))}</h1>${line}</span></div>`;
    if (query.trim()) return head + results();
    const people = state.friends.length ? `<div class="group friendrow" data-id="home.friends">` + state.friends.map(f =>
        `<span class="friend"><span class="pair">${bust(f.name, 'size-friend')}${presence(f.online)}</span>` +
        `<small>${escapeHTML(f.name)}</small></span>`).join('') + '</div>' :
        empty(tr('Aggiungi amici dalla pagina Amici.'), 'ld-icon-app-friends');
    return head + `<section class="shelf"><div class="shelfhead"><h2>${escapeHTML(tr('Amici ({0})', state.friends.length))}</h2>` +
        `<a class="button purple small" href="#friends">${tr('Vedi tutti')}</a></div>${people}</section>` +
        shelf('popular') + shelf('recent') + shelf('mine');
}
function experiences() {
    if (query.trim()) return results();
    if (listing) return `<button class="back" data-action="list-back"><span>${escapeHTML(backTitle())}</span></button>` +
        `<div class="listhead"><h1>${shelfTitles[listing]}</h1><span class="grow"></span><small>${tr('Genere')}</small>` +
        `<span class="popup"><select id="genre" aria-label="${tr('Genere')}"><option value="">${escapeHTML(tr('Tutti'))}</option>` +
        genres.map(g => `<option value="${escapeHTML(g)}" ${genre === g ? 'selected' : ''}>${escapeHTML(tr(g))}</option>`).join('') +
        '</select><i>' + icon('freccia-su') + icon('freccia-giu') + '</i></span></div>' + list(shelfEntries(listing));
    return `<h1>${tr('Esperienze')}</h1>` + ['featured', 'popular', 'recent', 'community'].map(shelf).join('');
}
const date = raw => {
    if (!raw) return '—';
    const d = new Date(raw);
    return isNaN(d) ? String(raw).slice(0, 10) :
        d.toLocaleDateString(LANGUAGE === 'it' ? 'it-IT' : 'en-US', {day: 'numeric', month: 'short', year: 'numeric'});
};
function experience(id) {
    const entry = catalog().find(e => e.id === id);
    if (!entry) return empty(tr('Esperienza non disponibile'));
    const details = experienceDetails[id], facts = details ? details.experience : entry;
    const stats = [[tr('In gioco'), details ? String(playing(entry)) : '—'], [tr('Visite'), details ? String(facts.visits) : '—'],
        [tr('Creata'), date(facts.createdAt)], [tr('Aggiornata'), date(facts.updatedAt)],
        [tr('Giocatori per server'), String(facts.serverSize || '—')], [tr('Genere'), tr(facts.genre)]];
    const servers = !details ? `<p class="loading">${tr('Cerco i server…')}</p>` : details.servers.length ?
        '<div class="group servers">' + details.servers.map(s => {
            const full = s.state === 'full';
            return `<div class="row server" data-server="${s.sequence}">${pack('ld-icon-app-players', 24)}<span class="words">` +
                `<b>${escapeHTML(tr('Server {0}', s.sequence))}</b><small>${escapeHTML(tr('{0} su {1}', s.players, s.capacity))}</small>` +
                `</span><span class="grow"></span><button class="${full ? 'white' : 'green'} small" data-action="join-server" ` +
                `data-id="${s.id}" ${full ? 'disabled' : ''}>${full ? tr('Pieno') : tr('Entra')}</button></div>`;
        }).join('') + '</div>' : `<div class="servers">${empty(tr(RULES.serverList.emptyTitle), 'ld-icon-app-players')}</div>`;
    const byline = (facts.author ? `<b class="author">${escapeHTML(tr('di {0}', facts.author))}</b><small>· ` : '<small>') +
        escapeHTML(tr(facts.genre)) + '</small>';
    const own = entry.tags.includes('mine') ? `<a class="button white wide" href="#scarica">${tr('Apri in Studio')}</a>` : '';
    return `<button class="back" data-action="list-back"><span>${escapeHTML(backTitle())}</span></button>` +
        `<div class="detail"><div class="picture"><span class="frame" data-id="experience.cover">${coverMarkup(entry, true)}</span>` +
        `<span class="mirror" style="--tint:${genreTint(entry.genre)}"></span></div><div class="side"><div class="heading">${appIcon(entry, 'detail')}<span><h1>` +
        `${escapeHTML(facts.title)}</h1><span class="byline">${byline}</span></span></div>` +
        `<button class="play-button" data-action="play" data-id="${id}" data-id-app="experience.play">${icon('gioca')}` +
        `${tr('Gioca')}</button>${own}</div></div><div class="group statbar" data-id="experience.statistics">` +
        stats.map(([label, value]) => `<div data-stat="${label}"><small>${label}</small><b>${escapeHTML(value)}</b></div>`).join('') +
        '</div><div class="centered">' + tabs({Informazioni: tr('Informazioni'), Server: tr('Server')}, experienceTab, 'experience-tab') +
        '</div>' + (experienceTab === 'Informazioni' ? `<p class="group description${entry.description ? '' : ' muted'}">` +
            `${escapeHTML(entry.description || tr('Nessuna descrizione.'))}</p>` :
            `<div class="servers-head"><h3>${tr('Server attivi')}</h3><span class="grow"></span>` +
            `<button class="white small" data-action="refresh-servers">${tr('Aggiorna')}</button></div>${servers}`);
}
function faceFor(a) {
    const worn = (a.equipped || []).find(e => e.category === 'face'), item = worn && collection.catalog.find(i => i.id === worn.itemID);
    return item && /^[A-Za-z]+\.png$/.test(item.asset || '') ? 'faces/' + item.asset : a.bodyPreset === 'female' ? 'faces/Female.png' :
        'faces/Male.png';
}
function figure(a, size = 180) {
    return '';
}
function palette(field, values, current) {
    return '<div class="swatches" role="group">' + values.map(v =>
        `<button class="swatch" style="--swatch:#${v}" aria-label="${field} ${v}" title="#${v}" aria-pressed="${current === v}" ` +
        `data-action="avatar-set" data-field="${field}" data-id="${v}"><span></span></button>`).join('') + '</div>';
}
function ownedItems() {
    const ids = new Set(collection.inventory.map(e => e.itemID));
    return collection.catalog.filter(i => ids.has(i.id));
}
function itemTile(i, offered) {
    const worn = draft.equipped.some(e => e.itemID === i.id), face = i.category === 'face' && /^[A-Za-z]+\.png$/.test(i.asset || '');
    const action = offered ? (i.free === true ? 'avatar-acquire' : '') : 'avatar-item';
    const label = offered ? (i.free === true ? `<span class="button green small">${tr('Ottieni')}</span>` :
        `<small class="muted">${tr('Non disponibile')}</small>`) :
        `<span class="button ${worn ? 'white' : 'purple'} small">${worn ? tr('Togli') : tr('Indossa')}</span>`;
    return `<button class="item${worn ? ' selected' : ''}" ${action ? `data-action="${action}" data-id="${escapeHTML(i.id)}"` : 'disabled'} ` +
        `title="${escapeHTML(i.name)}"><span class="shot${face ? ' skin' : ''}">` +
        (face ? `<img alt="" src="faces/${i.asset}">` : pack('ld-icon-app-catalog', 32)) +
        `</span><b>${escapeHTML(i.name)}</b>${label}</button>`;
}
function avatarEditor(catalog) {
    const a = draft, group = AV.groups[avatarTab], changed = JSON.stringify(draft) !== JSON.stringify(state.avatar);
    let rows = '';
    if (group) {
        const current = group.includes(avatarCategory) ? avatarCategory : group[0];
        const owned = ownedItems().filter(i => i.category === current && matching(i.name));
        const bodies = Object.fromEntries(AV.bodyPresets.map((v, i) => [v, tr(AV.bodyPresetTitles[i])]));
        rows = (avatarTab === 'body' ? `<div class="bodyrow"><label class="field"><b>${tr('Nome visualizzato')}</b>` +
            `<input id="avatar-name" maxlength="${AV.nameCharacters}" value="${escapeHTML(a.displayName)}"></label>` +
            `<div class="field"><b>${tr('Corporatura')}</b>${tabs(bodies, a.bodyPreset, 'avatar-body')}</div></div>` : '') +
            (group.length > 1 ? `<span class="popup"><select id="avatar-category" aria-label="${tr('Categoria')}">` +
                group.map(c => `<option value="${c}" ${c === current ? 'selected' : ''}>${escapeHTML(categoryTitle(c))}</option>`).join('') +
                '</select><i>' + icon('freccia-su') + icon('freccia-giu') + '</i></span>' : '') +
            (refusal ? `<p class="error">${escapeHTML(refusal)}</p>` : '') +
            (owned.length ? `<div class="items">${owned.map(i => itemTile(i, false)).join('')}</div>` :
                empty(query.trim() ? tr('Nessun risultato per «{0}».', query) : tr('Non hai ancora niente in {0}.', categoryTitle(current)),
                    'ld-icon-app-catalog'));
    } else rows = `<div class="actions"><button class="white small" data-action="avatar-default-colors">${tr('Colori predefiniti')}` +
        '</button></div>' + AV.bodyParts.map(part => `<div class="equip-row"><b>${partTitle(part)}</b>` +
            palette('color:' + part, AV.palette, a.bodyColors[part]) + '</div>').join('');
    return `<div class="avatar-layout"><div class="frame avatar-preview" data-id="avatar.preview.frame">${figure(a)}</div>` +
        `<div class="avatar-controls">${tabs(avatarTabs, avatarTab, 'avatar-tab')}${rows}<div class="actions">` +
        `<button class="purple" data-action="save-avatar" ${changed ? '' : 'disabled'}>${tr('Salva')}</button>` +
        `<button class="white" data-action="revert-avatar" ${changed ? '' : 'disabled'}>${tr('Annulla')}</button>` +
        (changed ? `<small>${tr('Modifiche non salvate')}</small>` : '') + `</div>${catalog}</div></div>`;
}
function avatar() {
    const offered = collection.catalog.filter(i => !collection.inventory.some(e => e.itemID === i.id) && matching(i.name));
    const catalog = `<h2>` + (tr('Catalogo')) + `</h2>` +
        (offered.length ? `<div class="items">${offered.map(i=>itemTile(i,true)).join('')}</div>` :
            empty(collection.catalog.length ? tr('Hai già tutto il catalogo!') : tr('Il catalogo è vuoto, per ora.'), 'ld-icon-app-catalog'));
    return `<h1>` + (tr('Avatar')) + `</h1>` + avatarEditor(catalog);
}
function friends() {
    const shown = state.friends.filter(f => matching(f.name)).sort((a, b) => Number(b.online) - Number(a.online));
    const pending = state.requests.filter(f => matching(f.name));
    const cards = friendsTab === 0 ? shown.map(f =>
        `<div class="group person" data-id="friends.card.${f.id}">${bust(f.name, 'size-friend')}<b>${escapeHTML(f.name)}</b>` +
        `<small>${presence(f.online)}${f.online ? tr('Online') : tr('Offline')}</small><button class="red small remove" ` +
        `data-action="remove-friend" data-id="${f.id}" title="${tr('Rimuovi dagli amici')}">${tr('Rimuovi')}</button></div>`) :
        pending.map(f => `<div class="group person wide">${bust(f.name, 'size-friend')}<b>${escapeHTML(f.name)}</b><span class="pairbuttons">` +
            `<button class="green small" data-action="accept" data-id="${f.id}">${tr('Accetta')}</button>` +
            `<button class="white small" data-action="reject" data-id="${f.id}">${tr('Rifiuta')}</button></span></div>`);
    const none = query.trim() ? tr('Nessun risultato per «{0}».', query) : friendsTab === 0 ?
        tr('Non hai ancora amici. Cercali per nome utente.') : tr('Nessuna richiesta di amicizia.');
    return `<h1>${tr('Amici')}</h1><div class="inline"><input id="friend-name" placeholder="${tr('Nome utente')}" ` +
        `aria-label="${tr('Nome utente')}"><button class="purple" data-action="request-friend">${tr('Invia richiesta')}</button></div>` +
        '<p id="friend-error" class="error" role="alert"></p>' +
        tabs({0: tr('Amici ({0})', state.friends.length), 1: tr('Richieste ({0})', state.requests.length)}, friendsTab, 'friends-tab') +
        (cards.length ? `<div class="people">${cards.join('')}</div>` :
            empty(none, query.trim() ? 'ld-icon-app-search' : 'ld-icon-app-friends'));
}
function settings() {
    const toggle = (key, on) => `<label class="switch${on ? ' on' : ''}"><input type="checkbox" data-setting="${key}" ` +
        `${on ? 'checked' : ''}><span class="track">${on ? tr('SÌ') : tr('NO')}</span><span class="knob"></span></label>`;
    const line = (title, detail, control) => `<div class="row"><span class="words"><b>${title}</b>` +
        `${detail ? `<small>${detail}</small>` : ''}</span><span class="grow"></span>${control || ''}</div>`;
    const flag = (key, title, detail) => line(title, detail, toggle(key, state.preferences[key]));
    const count = Array.from(state.bio || '').length;
    const pages = [
        `<div class="group">${line(tr('Password'), tr('Ti mandiamo un link all’email del tuo account.'))}<div class="inline field-row">` +
            `<input id="email" type="email" placeholder="${tr('Email')}" aria-label="${tr('Email')}">` +
            `<button class="white" data-action="password">${tr('Invia il link')}</button></div></div>` +
            '<div class="gap"></div><div class="group">' +
            `<a class="rowlink" href="${RULES.site.url}">${line(tr('Ludissey sul web'), tr('Accedi con lo stesso account sul sito.'),
                `<span class="value">${RULES.site.host}</span>${icon('freccia-destra')}`)}</a></div>` +
            `<div class="gap"></div><button class="red large wide" data-action="logout">${tr('Esci dall’account')}</button>` +
            (versions && versions.ludissey ? `<p class="footnote">Ludissey · ${escapeHTML(tr('Versione {0}', versions.ludissey.version))}</p>` : ''),
        `<div class="group">${flag('publicProfile', tr('Profilo pubblico'), tr('Tutti possono vedere nome, avatar e descrizione.'))}` +
            `</div><div class="gap"></div><h3>${tr('Descrizione')}</h3><div class="group bio"><textarea id="bio" maxlength="` +
            `${ACCOUNT.bioCharacters}" placeholder="${tr('Scrivi qualcosa su di te')}" aria-label="${tr('Descrizione')}">` +
            `${escapeHTML(state.bio)}</textarea>` +
            `<div class="inline"><small>${escapeHTML(tr('{0} di {1} caratteri', count, ACCOUNT.bioCharacters))}</small>` +
            `<span class="grow"></span><button class="purple small" data-action="save-bio">${tr('Salva')}</button></div></div>`,
        `<div class="group">${flag('friendNotifications', tr('Richieste di amicizia'),
            tr('Avvisami quando qualcuno vuole aggiungermi agli amici.'))}</div>`,
        `<h3>${tr('Audio')}</h3><div class="group">${flag('sounds', tr('Suoni dell’interfaccia'), '')}</div>`];
    return `<div class="settings-page" data-id="settings.page"><div class="settings"><h1>${tr('Impostazioni')}</h1>` +
        tabs({0: tr('Account'), 1: tr('Privacy'), 2: tr('Notifiche'), 3: tr('Audio e grafica')}, settingsTab, 'settings-tab') +
        `${pages[settingsTab]}</div></div>`;
}
const megabytes = bytes => (bytes/1048576).toLocaleString(LANGUAGE === 'it' ? 'it-IT' : 'en-US', {maximumFractionDigits: 1}) + ' MB';
const onMac = () => /Mac/.test(navigator.platform || '') && !(navigator.maxTouchPoints > 1);
function downloadCard(key, title, line, image) {
    const info = versions && versions[key], usable = !!info && onMac();
    return (`<div class="card download-card" id="scarica-` + (key) + `"><img alt="" src="download/` + (image) +
        `" width="96" height="96"><h2>` + (title) + `</h2><p>` + (line) + `</p>` +
        (info ?
            (`<small>` + (escapeHTML(tr('Versione {0}', info.version))) + `, ` + (megabytes(info.bytes)) + `</small><small>` +
                (escapeHTML(tr('Richiede macOS {0} o successivo', String(info.minimumSystem).replace(/\.0$/, '')))) + `</small>`):
            '') +
        (usable ?
            (`<a class="button green" href="download/` + (escapeHTML(info.file)) + `" download>` + (tr('Scarica per Mac')) + `</a>`):
            (`<button class="green" disabled>` + (!onMac() ? tr('Disponibile solo per Mac') : tr('Download non ancora disponibile')) +
                `</button>`)) +
        `</div>`);
}
function downloads() {
    return (`<section class="downloads" id="scarica"><h1>` + (tr('Scarica Ludissey')) + `</h1><div class="download-cards">` +
        (downloadCard('ludissey', 'Ludissey', tr('Gioca alle esperienze della community.'), 'Ludissey.png')) +
        (downloadCard('studio', 'Ludissey Studio', tr('Costruisci e pubblica i tuoi mondi.'), 'Ludissey-Studio.png')) +
        `</div></section>`);
}
function dropdown() {
    const unread = state.notifications.some(n => n.unread);
    const rows = state.notifications.length ? state.notifications.map(n =>
        `<div class="note">${pack('ld-icon-app-friends', 24)}<span class="name"><b class="${n.unread ? '' : 'read'}">` +
        `${escapeHTML(n.title)}</b><small>${escapeHTML(n.body || '')}</small>${n.at ? `<small class="when">${escapeHTML(date(n.at))}` +
        '</small>' : ''}</span>${n.unread ? '<span class="unread"></span>' : ''}<button class="close" data-action="dismiss" ` +
        `data-id="${n.id}" aria-label="${tr('Rimuovi notifica')}">${icon('chiudi')}</button></div>`).join('') :
        `<p class="none" data-id="notifications.empty">${pack('ld-icon-app-notifications', 24)}${tr('Nessuna notifica.')}</p>`;
    const action = unread ? `<button class="bar" data-action="notifications-read">${tr('Segna come lette')}</button>` : '';
    return `<div class="popover notifications" data-id="notifications"><span class="arrow"></span><div class="ph">` +
        `<b>${tr('Notifiche')}</b><span class="grow"></span>${action}</div><div class="pbody">${rows}</div></div>`;
}
function topbar() {
    const unread = state.notifications.filter(n => n.unread).length, prompt = searchPrompt();
    const search = prompt ? `<label class="search${searchOpen ? ' open' : ''}" data-id="utility.searchField">` +
        pack('ld-icon-app-search', 16) + `<input type="search" id="search" aria-label="${prompt}" placeholder="${prompt}" ` +
        `value="${escapeHTML(query)}"></label><button class="bar search-toggle" data-action="search-toggle" aria-label="${prompt}" ` +
        `aria-expanded="${searchOpen}">${pack('ld-icon-app-search', 22)}</button>` : '';
    const menu = accountMenu ? `<div class="popover menu" data-id="account.menu"><span class="arrow"></span><div class="pbody">` +
        `<a class="menurow" href="#settings">${tr('Impostazioni')}</a><button class="menurow" data-action="logout">${tr('Esci')}` +
        '</button></div></div>' : '';
    return `<header class="topnav${menuOpen ? ' open' : ''}" data-id="topNavigation"><a class="logo" href="#home" ` +
        `data-id="topNavigation.logo" aria-label="Ludissey">${brand()}</a><button class="bar menu-toggle" data-action="menu" ` +
        `aria-label="${tr('Menu')}" aria-expanded="${menuOpen}">${icon('freccia-giu')}</button><nav class="navgroup" ` +
        `aria-label="${tr('Sezioni')}">${primary.map(nav).join('')}<a class="navlink" href="#scarica" ` +
        `data-id="topNavigation.create"><span class="face"><span>${tr('Crea')}</span></span></a></nav><div class="grow"></div>${search}` +
        `<a class="download" href="#scarica">${tr('Scarica')}</a><div class="dropdown-wrap"><button class="bar" ` +
        `data-action="notifications" data-id="utility.notifications" aria-label="` +
        `${unread ? tr('{0} notifiche da leggere', unread) : tr('Notifiche')}" aria-expanded="${notificationsOpen}">` +
        `${pack('ld-icon-app-notifications', 22)}${unread ? `<span class="badge">${unread}</span>` : ''}</button>` +
        `${notificationsOpen ? dropdown() : ''}</div><div class="dropdown-wrap"><button class="bar" data-action="account-menu" ` +
        `data-id="topNavigation.settings" aria-label="${tr('Impostazioni')}" aria-expanded="${accountMenu}">` +
        `${pack('ld-icon-app-settings', 22)}</button>${menu}</div><a class="profile" href="#avatar" data-id="topNavigation.profile" ` +
        `title="${tr('Avatar')}"><span class="who">${escapeHTML(state.username)}</span>${bust(state.username, 'size-header')}</a></header>`;
}
function characterChoice() {
    return `<div class="characters"><b>${tr('Scegli il personaggio')}</b><div class="character-row">` + ['male', 'female'].map(g =>
        `<button type="button" class="character${gender === g ? ' selected' : ''}" data-action="gender" data-id="${g}" ` +
        `aria-pressed="${gender === g}"><span class="skin"><img alt="" src="faces/${g === 'male' ? 'Male' : 'Female'}.png"></span>` +
        `<b>${g === 'male' ? tr('Maschile') : tr('Femminile')}</b><span class="check">${icon('spunta')}</span></button>`).join('') +
        '</div></div>';
}
function login() {
    const busy = pendingOperations ? 'disabled' : '';
    const entry = (id, name, label, prompt, extra) => `<label class="row entry"><b>${label}</b><input id="${id}" name="${name}" ` +
        `${extra} placeholder="${prompt}" aria-label="${prompt}"></label>`;
    const back = action => `<button type="button" class="plain" data-action="${action}">${tr('Torna indietro')}</button>`;
    const panel = needsProfile ? `<form class="authcard" id="profile-form" data-id="login.card"><h1>` +
        `${tr('Ultimo passo: scegli il tuo personaggio')}</h1>${characterChoice()}<p id="register-error" class="error" role="alert">` +
        `${escapeHTML(registerMessage)}</p><button class="green large wide" type="submit" ${busy}>${tr('Entra in Ludissey')}</button>` +
        `${back('logout-now')}</form>` : recovering ? `<form class="authcard" id="recovery-form" data-id="login.card" novalidate>` +
        `<h1>${tr('Recupera la password')}</h1><p class="muted">` +
        `${tr('Scrivi l’email del tuo account: ti mandiamo un link per cambiare password.')}</p><div class="group">` +
        entry('recovery-email', 'email', tr('Email'), tr('nome@esempio.it'), 'type="email" required') + '</div>' +
        `<p id="register-error" class="error" role="alert">${escapeHTML(registerMessage)}</p><button class="green large wide" ` +
        `type="submit" ${busy}>${tr('Invia il link')}</button>${back('recover-back')}</form>` :
        `<form class="authcard" id="register-form" data-id="login.card" novalidate><h1>${tr('Registrati e gioca')}</h1>` +
        '<div class="group">' + entry('register-username', 'username', tr('Nome'), tr('Nome utente'),
            'minlength="3" maxlength="20" required autocomplete="username"') +
        entry('register-password', 'password', tr('Password'), tr('Almeno 6 caratteri'),
            'type="password" minlength="6" maxlength="100" required autocomplete="new-password"') +
        entry('register-confirm', 'confirm', tr('Conferma'), tr('Ripeti la password'),
            'type="password" minlength="6" maxlength="100" required autocomplete="new-password"') + '</div>' +
        `<p id="register-error" class="error" role="alert">${escapeHTML(registerMessage)}</p>` +
        '<p id="password-error" class="error" role="alert"></p><p id="confirm-error" class="error" role="alert"></p>' +
        `${characterChoice()}<button class="green large wide register-submit" type="submit" ${busy}>${tr('Registrati')}</button></form>`;
    const inline = needsProfile ? '' : `<button class="purple login-toggle" type="button" data-action="login-toggle">${tr('Accedi')}` +
        `</button><form class="inline-login${loginOpen ? ' open' : ''}" id="auth-form" novalidate><input id="username" ` +
        `name="username" minlength="3" maxlength="20" required autocomplete="username" placeholder="${tr('Nome utente')}" ` +
        `aria-label="${tr('Nome utente')}"><span class="secret"><input id="password" name="password" type="password" minlength="6" ` +
        `maxlength="100" required autocomplete="current-password" placeholder="${tr('Password')}" aria-label="${tr('Password')}">` +
        `<button type="button" class="forgot" data-action="recover">${tr('Password dimenticata?')}</button></span>` +
        `<button class="purple submit" type="submit" ${busy}>${tr('Accedi')}</button></form>`;
    return `<main class="authwrap" id="content"><header class="loginbar" data-id="login.bar">${brand()}<span class="grow"></span>` +
        `<a class="download" href="#scarica">${tr('Scarica')}</a>${inline}</header><p id="auth-error" class="error under" ` +
        `role="alert">${escapeHTML(authMessage)}</p><div class="hero"><div class="pitch" data-id="login.brand">${brand()}<p>` +
        `<span>${tr('Gioca ai mondi della community.')}</span><span>${tr('Poi costruisci il tuo.')}</span></p></div>${panel}</div>` +
        `</main>${downloads()}`;
}
const pages = {home, experiences, avatar, friends, settings, scarica: downloads};
function page() {
    return route.startsWith('experience/') ? experience(route.slice(11)) : pages[route]();
}
function renderContent() {
    const target = document.getElementById('page');
    if (!target) return;
    target.innerHTML = page();
    hydrateMedia();
}
function render() {
    if (!linked() || needsProfile) {
        document.title = tr('Accesso · Ludissey');
        app.innerHTML = login();
        if (location.hash === '#scarica') document.getElementById('scarica').scrollIntoView();
        return;
    }
    if (!stateReady) {
        app.innerHTML = (`<main class="connection-screen"><div class="panel">` + (brand()) + `<h1>` + tr('Carico…') +
            `</h1><p id="connection-status" role="status">` + (escapeHTML(statusMessage)) + `</p>` +
            `<button data-action="retry-connection">${tr('Riprova')}</button>` +
            `<button data-action="logout-now">${tr('Esci')}</button></div></main>`);
        return;
    }
    if (!routes[route] && route !== 'scarica' && !route.startsWith('experience/')) route = 'home';
    document.title = (route.startsWith('experience/') ? tr('Esperienza') : route === 'scarica' ? tr('Scarica') : routes[route]) +
        ' · Ludissey';
    app.innerHTML = topbar() +
        (`<main id="content"><p class="status` + (statusMessage ? ' error' : '') + `" id="connection-status" role="status" ` +
            (statusMessage ? '' : 'hidden') + `>` + (escapeHTML(statusMessage)) +
            `</p><div id="page"></div></main>`);
    renderContent();
    for (const popover of document.querySelectorAll('.popover')) {
        const anchor = popover.parentElement.querySelector('.bar').getBoundingClientRect(), frame = popover.getBoundingClientRect();
        popover.style.setProperty('--arrow', (anchor.left + anchor.width / 2 - frame.left) + 'px');
    }
}
function navigate(id) {
    if (route === id) {
        query = '';
        render();
    } else location.hash = id;
}
function openPage(next, nextListing) {
    trail.push({route, listing});
    listing = nextListing;
    notificationsOpen = false;
    if (route === next) render();
    else {
        keepTrail = true;
        location.hash = next;
    }
}
function goBack() {
    const fallback = route.startsWith('experience/') || route.startsWith('profile/') ? 'experiences' : route;
    const target = trail.pop() || {route: fallback, listing: null};
    listing = target.listing;
    genre = null;
    if (route === target.route) render();
    else {
        keepTrail = true;
        location.hash = target.route;
    }
}
function backTitle() {
    const previous = trail[trail.length - 1];
    if (!previous) return tr('Esperienze');
    if (previous.route.startsWith('experience/')) {
        const entry = catalog().find(e => e.id === previous.route.slice(11));
        return entry ? entry.title : tr('Esperienze');
    }
    if (previous.route.startsWith('profile/')) return decodeURIComponent(previous.route.slice(8));
    if (previous.listing) return shelfTitles[previous.listing];
    return routes[previous.route] || tr('Esperienze');
}
function showDialog(title, body, actions = [{label: tr('OK')}], warning = false) {
    lastFocus = document.activeElement;
    const heading = document.getElementById('dialog-title'), text = document.getElementById('dialog-body');
    heading.textContent = title;
    heading.hidden = !title;
    text.textContent = body;
    text.hidden = !body;
    dialog.querySelector('.alert-icon')?.remove();
    if (warning) heading.insertAdjacentHTML('beforebegin', `<span class="alert-icon">${pack('ld-icon-status-warning', 34)}</span>`);
    const target = document.getElementById('dialog-actions');
    target.replaceChildren();
    actions.forEach((action, index) => {
        const b = document.createElement('button');
        b.textContent = action.label;
        b.className = 'ab' + (index === actions.length - 1 && actions.length > 1 ? ' default' : '');
        b.onclick = () => {
            dialog.close();
            action.run?.();
        };
        target.append(b);
    });
    dialog.showModal();
}
function appOpened() {
    return new Promise(resolve => {
        let done = false;
        const finish = value => {
            if (done) return;
            done = true;
            window.removeEventListener('blur', blurred);
            document.removeEventListener('visibilitychange', hidden);
            resolve(value);
        };
        const blurred = () => finish(true);
        const hidden = () => {
            if (document.hidden) finish(true);
        };
        window.addEventListener('blur', blurred);
        document.addEventListener('visibilitychange', hidden);
        setTimeout(() => finish(document.hidden || !document.hasFocus()), 1500);
    });
}
function play(id, server) {
    return operation(async() => {
        const watching = appOpened();
        openInApp(id, server);
        if (await watching) return;
        showDialog(tr('Per giocare ti serve l’app Ludissey'), '',
            [{label: tr('Ce l’ho già, riprova'), run: () => play(id, server)}, {label: tr('Scarica Ludissey'), run: () => {
            location.hash = 'scarica';
        }
        }]);
    });
}
function toggleItem(id) {
    const item = collection.catalog.find(i => i.id === id);
    refusal = '';
    if (!item) return;
    if (draft.equipped.some(e => e.itemID === id)) {
        draft.equipped = draft.equipped.filter(e => e.itemID !== id);
        return;
    }
    const wanted = {...draft, equipped: draft.equipped.concat([{itemID: item.id, category: item.category}])};
    const valid = LudisseyRules.avatar(wanted, draft.displayName, draft.bodyPreset, collection.inventory, collection.catalog);
    if (valid.equipped.some(e => e.itemID === item.id)) {
        draft.equipped = valid.equipped;
        return;
    }
    const limit = LudisseyRules.avatarLimit(item.category);
    refusal = draft.equipped.filter(e => e.category === item.category).length >= limit ?
        tr('Puoi indossare al massimo {0} oggetti della categoria {1}.', limit, categoryTitle(item.category)):
        tr('Puoi indossare al massimo {0} accessori.', AV.limits.accessories);
}
function accountAction(action, id) {
    if (action === 'save-avatar') {
        operation(async() => {
            const reply = await cloud('LudisseySaveAvatar', {avatar: draft});
            state.avatar = reply.avatar;
            draft = JSON.parse(JSON.stringify(reply.avatar));
            showDialog('', tr('Avatar salvato!'));
        });
        return true;
    }
    if (action === 'avatar-acquire') {
        operation(() => cloud('LudisseyAcquireAvatarItem', {itemId: id}));
        return true;
    }
    if (action === 'save-bio') {
        const bio = document.getElementById('bio').value;
        operation(() => saveBio(bio));
        return true;
    }
    if (action === 'request-friend') {
        const username = document.getElementById('friend-name').value.trim();
        const problem = document.getElementById('friend-error');
        if (username.length < 3 || username.length > 20) {
            problem.textContent = tr('Il nome utente deve avere da 3 a 20 caratteri.');
            document.getElementById('friend-name').focus();
            return true;
        }
        problem.textContent = '';
        operation(async() => {
            await sendFriendRequest(username);
            showDialog('', tr('Richiesta inviata!'));
        });
        return true;
    }
    if (action === 'password') {
        const email = document.getElementById('email').value.trim();
        operation(async() => {
            await recoverPassword(email);
            showDialog('', tr('Se l’email è giusta, tra poco ti arriva il link.'));
        });
        return true;
    }
    if (action === 'notifications') {
        notificationsOpen = !notificationsOpen;
        accountMenu = false;
        render();
        return true;
    }
    if (action === 'notifications-read') {
        operation(() => markNotifications());
        return true;
    }
    if (action === 'dismiss') {
        operation(() => markNotifications(id));
        return true;
    }
    if (action === 'accept' || action === 'reject') {
        operation(() => cloud('LudisseyRespondFriend', {id, accept: action === 'accept'}));
        return true;
    }
    if (action === 'remove-friend') {
        showDialog(tr('Rimuovere {0}?', (state.friends.find(f => f.id === id) || {name: ''}).name),
            tr('Non sarete più amici. Puoi sempre richiederlo di nuovo.'),
            [{label: tr('Annulla')},
                {label: tr('Rimuovi'), run: () => operation(() => cloud('LudisseyRemoveFriend', {id}))}]);
        return true;
    }
    if (action === 'logout') {
        showDialog(tr('Esci dall’account?'), tr('Dovrai accedere di nuovo.'),
            [{label: tr('Annulla')}, {label: tr('Esci'), run: () => {
                endSession();
                render();
            }}]);
        return true;
    }
    if (action === 'refresh-servers') {
        refresh();
        return true;
    }
    return false;
}
dialog.addEventListener('close', () => {
    if (lastFocus?.isConnected) lastFocus.focus();
});
window.addEventListener('hashchange', () => {
    route = location.hash.slice(1) || 'home';
    if (!keepQuery) query = '';
    if (!keepTrail) {
        trail = [];
        listing = null;
        genre = null;
    }
    keepTrail = false;
    keepQuery = false;
    notificationsOpen = false;
    accountMenu = false;
    menuOpen = false;
    render();
    if (route !== 'scarica' || linked()) window.scrollTo(0, 0);
    refresh();
});
app.addEventListener('input', event => {
    if (event.target.closest('#auth-form,#register-form')) return;
    if (event.target.id === 'avatar-name') {
        draft.displayName = event.target.value;
        const save = document.querySelector('[data-action=save-avatar]'), revert = document.querySelector('[data-action=revert-avatar]');
        const changed = JSON.stringify(draft) !== JSON.stringify(state.avatar);
        if (save) save.disabled = !changed;
        if (revert) revert.disabled = !changed;
        return;
    }
    if (event.target.id === 'search') {
        query = event.target.value;
        if (route.startsWith('experience/') || route.startsWith('profile/') || listing) {
            trail = [];
            listing = null;
            keepQuery = true;
            if (route === 'experiences') render();
            else location.hash = 'experiences';
            return;
        }
        renderContent();
    }
});
app.addEventListener('change', event => {
    const target = event.target;
    if (target.id === 'genre') {
        genre = target.value || null;
        renderContent();
        return;
    }
    if (target.id === 'avatar-category') {
        avatarCategory = target.value;
        refusal = '';
        renderContent();
        return;
    }
    if (target.dataset.setting) {
        const key = target.dataset.setting, value = target.checked;
        operation(() => savePreference(key, value));
    }
});
app.addEventListener('submit', event => {
    const form = event.target, report = (id, message, field) => {
        for (const node of form.ownerDocument.querySelectorAll('.error')) if (node.id !== id) node.textContent = '';
        const node = document.getElementById(id);
        if (node) node.textContent = message;
        document.getElementById(field)?.focus();
    };
    event.preventDefault();
    if (form.id === 'profile-form') {
        register = true;
        authenticate('', '');
        return;
    }
    if (form.id === 'recovery-form') {
        const email = document.getElementById('recovery-email').value.trim();
        if (pendingOperations) return;
        if (!email.includes('@') || email.length > 254) {
            report('register-error', tr('Scrivi un’email valida.'), 'recovery-email');
            return;
        }
        pendingOperations++;
        recoverPassword(email).then(() => {
            registerMessage = '';
            recovering = false;
            showDialog('', tr('Se l’email è giusta, tra poco ti arriva il link.'));
        }).catch (error => {
            registerMessage = error.message;
        }).finally(() => {
            pendingOperations--;
            render();
        });
        return;
    }
    if (form.id !== 'auth-form' && form.id !== 'register-form') return;
    const data = new FormData(form);
    const name = String(data.get('username') || '').trim();
    const password = String(data.get('password') || '');
    const registering = form.id === 'register-form';
    if (name.length < 3 || name.length > 20) {
        report(registering ? 'register-error' : 'auth-error', tr('Il nome utente deve avere da 3 a 20 caratteri.'),
            registering ? 'register-username' : 'username');
        return;
    }
    if (password.length < 6 || password.length > 100) {
        report(registering ? 'password-error' : 'auth-error', tr('La password deve avere almeno 6 caratteri.'),
            registering ? 'register-password' : 'password');
        return;
    }
    if (registering && password !== data.get('confirm')) {
        report('confirm-error', tr('Le password non coincidono'), 'register-confirm');
        return;
    }
    register = registering;
    authenticate(name, password);
});
app.addEventListener('click', event => {
    const link = event.target.closest('a[href^="#"]');
    if (link && link.getAttribute('href').slice(1) === route && (trail.length || listing)) {
        event.preventDefault();
        trail = [];
        listing = null;
        genre = null;
        query = '';
        notificationsOpen = false;
        accountMenu = false;
        render();
        return;
    }
    const button = event.target.closest('[data-action]');
    if (!button) return;
    const {action, id} = button.dataset;
    if (action === 'retry-connection') {
        refresh();
        return;
    }
    if (action === 'logout-now') {
        endSession();
        render();
        return;
    }
    if (action === 'gender') {
        gender = id;
        button.parentElement.querySelectorAll('button').forEach(b => {
            b.classList.toggle('selected', b.dataset.id === id);
            b.setAttribute('aria-pressed', String(b.dataset.id === id));
        });
        return;
    }
    if (action === 'login-toggle') {
        loginOpen = !loginOpen;
        document.getElementById('auth-form').classList.toggle('open', loginOpen);
        return;
    }
    if (action === 'recover') {
        recovering = true;
        registerMessage = '';
        render();
        return;
    }
    if (action === 'recover-back') {
        recovering = false;
        registerMessage = '';
        render();
        return;
    }
    if (!linked() || !stateReady) return;
    if (accountAction(action, id)) return;
    if (action === 'experience') {
        experienceTab = 'Informazioni';
        openPage('experience/' + id, null);
        return;
    }
    if (action === 'experience-tab') {
        experienceTab = id;
        renderContent();
        return;
    }
    if (action === 'play') {
        play(id);
        return;
    }
    if (action === 'join-server') {
        play(route.slice(11), id);
        return;
    }
    if (action === 'see-all') {
        genre = null;
        openPage('experiences', id);
        return;
    }
    if (action === 'list-back') {
        goBack();
        return;
    }
    if (action === 'friends-tab') {
        friendsTab = Number(id);
        renderContent();
        return;
    }
    if (action === 'settings-tab') {
        settingsTab = Number(id);
        renderContent();
        return;
    }
    if (action === 'account-menu') {
        accountMenu = !accountMenu;
        notificationsOpen = false;
        render();
        return;
    }
    if (action === 'menu') {
        menuOpen = !menuOpen;
        render();
        return;
    }
    if (action === 'search-toggle') {
        searchOpen = !searchOpen;
        render();
        return;
    }
    if (action === 'avatar-tab') {
        avatarTab = id;
        renderContent();
        return;
    }
    if (action === 'avatar-body') {
        draft.bodyPreset = id;
        renderContent();
        return;
    }
    if (action === 'avatar-set') {
        const field = button.dataset.field;
        if (field.startsWith('color:')) draft.bodyColors[field.slice(6)] = id;
        else draft[field] = id;
        renderContent();
        return;
    }
    if (action === 'avatar-item') {
        toggleItem(id);
        renderContent();
        return;
    }
    if (action === 'avatar-default-colors') {
        draft.bodyColors = {...AV.standard.bodyColors};
        renderContent();
        return;
    }
    if (action === 'revert-avatar') {
        draft = JSON.parse(JSON.stringify(state.avatar));
        renderContent();
        return;
    }
});
fetch('download/versions.json', {cache: 'no-store'}).then(reply => reply.ok ? reply.json() : null).then(data => {
    versions = data && typeof data === 'object' ? data : null;
    const shown = document.getElementById('scarica');
    if (shown) shown.outerHTML = downloads();
}).catch (() => {
});
render();
if (linked()) {
    refresh();
    startPolling();
}
