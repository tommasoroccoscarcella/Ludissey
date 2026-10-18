function fail(message) { throw message; }
function text(value, max) {
    if (typeof value !== 'string' || value.trim().length < 1 || value.length > max) fail('Testo non valido');
    return value.trim();
}
function read(id, keys) { return server.GetUserReadOnlyData({PlayFabId:id, Keys:keys}).Data || {}; }
function value(data, key, fallback) { return data[key] ? JSON.parse(data[key].Value) : fallback; }
function save(id, data) {
    var encoded = {};
    Object.keys(data).forEach(function(k) { encoded[k] = JSON.stringify(data[k]); });
    server.UpdateUserReadOnlyData({PlayFabId:id, Data:encoded, Permission:'Private'});
}
function profile(id) { return value(read(id, ['LudisseyProfile']), 'LudisseyProfile', null); }
function active() { var p=profile(currentPlayerId); if (!p) fail('Completa il tuo avatar'); return p; }
handlers.LudisseyBootstrap=function(args) {
    var p=profile(currentPlayerId);
    if (!p) {
        if (!args || ['male','female'].indexOf(args.gender)<0) return {needsProfile:true};
        var account=server.GetUserAccountInfo({PlayFabId:currentPlayerId}).UserInfo;
        p={username:account.Username || account.TitleInfo.DisplayName || currentPlayerId,gender:args.gender,createdAt:new Date().toISOString()};
        save(currentPlayerId,{LudisseyProfile:p});
    }
    return {profile:p,id:currentPlayerId};
};
function avatarCatalog() {
    var data = server.GetTitleData({Keys:['LudisseyAvatarCatalog']}).Data || {};
    var list = data.LudisseyAvatarCatalog ? JSON.parse(data.LudisseyAvatarCatalog) : [];
    if (!Array.isArray(list)) return [];
    return list.filter(function(item) {
        return item && typeof item.id === 'string' && typeof item.name === 'string' && RULES.avatar.categories.indexOf(item.category) >= 0 && (item.asset === undefined || ASSET.test(item.asset));
    });
}
function avatarInventory(id) {
    var list = value(read(id, ['LudisseyAvatarInventory']), 'LudisseyAvatarInventory', []);
    return Array.isArray(list) ? list : [];
}
function avatarCollection(id) { return {catalog:avatarCatalog(), inventory:avatarInventory(id)}; }
handlers.LudisseyAvatar = function() {
    var p = active(), collection = avatarCollection(currentPlayerId);
    var stored = value(read(currentPlayerId, ['LudisseyAvatar']), 'LudisseyAvatar', null);
    if (!stored) {
        var legacy = (server.GetUserData({PlayFabId:currentPlayerId, Keys:['LudisseyAvatar']}).Data || {}).LudisseyAvatar;
        stored = legacy ? JSON.parse(legacy.Value) : null;
    }
    return {avatar:stored ? avatarValidate(stored, p.username, p.gender, collection.inventory, collection.catalog) : null, catalog:collection.catalog, inventory:collection.inventory};
};
handlers.LudisseySaveAvatar = function(args) {
    var p = active(), collection = avatarCollection(currentPlayerId);
    var avatar = avatarValidate(args.avatar, p.username, p.gender, collection.inventory, collection.catalog);
    save(currentPlayerId, {LudisseyAvatar:avatar});
    return {avatar:avatar};
};
handlers.LudisseyPlayerAvatar = function(args) {
    active();
    var target = profile(text(args.id, 32));
    if (!target) fail('Giocatore non trovato');
    var collection = avatarCollection(args.id);
    var stored = value(read(args.id, ['LudisseyAvatar']), 'LudisseyAvatar', null);
    return {avatar:avatarValidate(stored, target.username, target.gender, collection.inventory, collection.catalog)};
};
handlers.LudisseyAcquireAvatarItem = function(args) {
    active();
    var item = avatarCatalog().filter(function(i) { return i.id === args.itemId; })[0];
    if (!item) fail('Oggetto non presente nel catalogo');
    if (item.free !== true) fail('Oggetto non disponibile gratuitamente');
    var inventory = avatarInventory(currentPlayerId);
    if (!inventory.some(function(e) { return e.itemID === item.id; })) {
        inventory.push({id:identifier(), itemID:item.id, acquiredAt:iso(now())});
        save(currentPlayerId, {LudisseyAvatarInventory:inventory});
    }
    return avatarCollection(currentPlayerId);
};
handlers.LudisseyExperienceData = function(args) {
    active();
    var w = experience(args.experienceId);
    var key = 'LudisseyData_' + w.id.replace(/-/g, '');
    if (args.action === 'load') return {value:value(read(currentPlayerId, [key]), key, null)};
    if (args.action !== 'save') refuse('InvalidData', 'Operazione sui dati non valida');
    if (!('value' in args) || JSON.stringify(args.value).length > RULES.scriptData.characters)
        refuse('InvalidData', 'Dati troppo grandi: massimo ' + RULES.scriptData.characters + ' caratteri');
    var all = server.GetUserReadOnlyData({PlayFabId:currentPlayerId}).Data || {};
    if (!all[key] && Object.keys(all).filter(function(k) { return k.indexOf('LudisseyData_') === 0; }).length >= RULES.scriptData.experiences)
        refuse('InvalidData', 'Troppe esperienze con dati salvati');
    var data = {};
    data[key] = args.value;
    save(currentPlayerId, data);
    return {saved:true};
};
handlers.LudisseyFriends=function() {
    active();
    var data=read(currentPlayerId,['LudisseyRequests']);
    return {friends:server.GetFriendsList({PlayFabId:currentPlayerId}).Friends || [],requests:value(data,'LudisseyRequests',[])};
};
handlers.LudisseyRequestFriend=function(args) {
    var p=active();
    var target=server.GetUserAccountInfo({PlayFabId:text(args.id,32)}).UserInfo.PlayFabId;
    if (target===currentPlayerId) fail('Non puoi aggiungere te stesso');
    if (!profile(target)) fail('Questo giocatore non ha ancora finito di registrarsi.');
    var friends=server.GetFriendsList({PlayFabId:currentPlayerId}).Friends || [];
    if (friends.some(function(f) {return f.FriendPlayFabId===target;})) fail('Siete già amici');
    var requests=value(read(target,['LudisseyRequests']),'LudisseyRequests',[]);
    if (!requests.some(function(r) {return r.id===currentPlayerId;})) {
        if (requests.length>=50) fail('Questo giocatore ha già troppe richieste di amicizia.');
        requests.push({id:currentPlayerId,username:p.username,at:iso(now())}); save(target,{LudisseyRequests:requests});
    }
    return {sent:true};
};
handlers.LudisseyRespondFriend=function(args) {
    active();
    var requests=value(read(currentPlayerId,['LudisseyRequests']),'LudisseyRequests',[]);
    if (!requests.some(function(r) {return r.id===args.id;})) fail('Richiesta non trovata');
    if (args.accept===true) {
        server.AddFriend({PlayFabId:currentPlayerId,FriendPlayFabId:args.id});
        server.AddFriend({PlayFabId:args.id,FriendPlayFabId:currentPlayerId});
    }
    save(currentPlayerId,{LudisseyRequests:requests.filter(function(r) {return r.id!==args.id;})});
    return {accepted:args.accept===true};
};
handlers.LudisseyRemoveFriend=function(args) {
    active();
    var friends=server.GetFriendsList({PlayFabId:currentPlayerId}).Friends || [];
    if (!friends.some(function(f) {return f.FriendPlayFabId===args.id;})) fail('Amicizia non trovata');
    server.RemoveFriend({PlayFabId:currentPlayerId,FriendPlayFabId:args.id});
    server.RemoveFriend({PlayFabId:args.id,FriendPlayFabId:currentPlayerId});
    return {removed:true};
};
function known(id) {
    try { return profile(id); } catch (missing) { return null; }
}
handlers.LudisseyPlayerProfile=function(args) {
    active();
    var id=text(args.id,32), target=known(id);
    if (!target) fail('Giocatore non trovato');
    var self=id===currentPlayerId;
    var list=server.GetFriendsList({PlayFabId:id}).Friends || [];
    var friend=!self && list.some(function(f) {return f.FriendPlayFabId===currentPlayerId;});
    var asked=function(owner, who) {return value(read(owner,['LudisseyRequests']),'LudisseyRequests',[]).some(function(r) {return r.id===who;});};
    var data=server.GetUserData({PlayFabId:id,Keys:['LudisseyPreferences','LudisseyPublicProfile','LudisseyPresence']}).Data || {};
    var preferences={};
    try { preferences=data.LudisseyPreferences ? JSON.parse(data.LudisseyPreferences.Value) : {}; } catch (broken) { preferences={}; }
    var seen=data.LudisseyPresence ? Number(data.LudisseyPresence.Value) || 0 : 0, age=now()/1000-seen;
    var result={id:id,username:target.username,gender:target.gender==='female' ? 'female' : 'male',createdAt:target.createdAt || null,self:self,friend:friend,
        requested:!self && asked(id,currentPlayerId),requesting:!self && asked(currentPlayerId,id),publicProfile:!!data.LudisseyPublicProfile,friends:list.length,
        online:seen>0 && age>=0 && age<RULES.account.presenceSeconds,experiences:listing().filter(function(e) {return e.owner===id;})};
    if (result.publicProfile || friend) {
        result.bio=typeof preferences.bio==='string' ? preferences.bio.slice(0,RULES.account.bioCharacters) : '';
        result.friendList=list.slice(0,6).map(function(f) {
            var other=known(f.FriendPlayFabId);
            return {id:f.FriendPlayFabId,username:other ? other.username : (f.Username || f.FriendPlayFabId),gender:other && other.gender==='female' ? 'female' : 'male'};
        });
    }
    return result;
};
var runtime = {
    region: 'global',
    clock: function() { return Date.now(); },
    random: function() {
        var out = '';
        while (out.length < 64) out += ('0000000' + Math.floor(Math.random() * 4294967296).toString(16)).slice(-8);
        return out;
    }
};
var IDENTIFIER = /^[a-f0-9-]{36}$/;
var ASSET = /^[a-f0-9]{64}\.[a-z0-9]{1,5}$/;
function image(v) { return typeof v === 'string' && ASSET.test(v) && RULES.assets.imageExtensions.indexOf(v.slice(65)) >= 0; }
function refuse(code, message) { throw '[' + code + '] ' + message; }
function now() { return runtime.clock(); }
function iso(moment) { return new Date(moment).toISOString(); }
function identifier() {
    var r = runtime.random();
    return r.slice(0, 8) + '-' + r.slice(8, 12) + '-4' + r.slice(13, 16) + '-' + '89ab'.charAt(parseInt(r.charAt(16), 16) % 4) + r.slice(17, 20) + '-' + r.slice(20, 32);
}
function sha256(ascii) {
    function rotate(v, n) { return (v >>> n) | (v << (32 - n)); }
    var k = [], h = [], i, j, found = 0, candidate = 2;
    while (found < 64) {
        var prime = true;
        for (j = 2; j * j <= candidate; j++) if (candidate % j === 0) { prime = false; break; }
        if (prime) {
            if (found < 8) h[found] = (Math.pow(candidate, 1 / 2) * 4294967296) | 0;
            k[found++] = (Math.pow(candidate, 1 / 3) * 4294967296) | 0;
        }
        candidate++;
    }
    var bytes = [];
    for (i = 0; i < ascii.length; i++) bytes.push(ascii.charCodeAt(i) & 255);
    var bits = bytes.length * 8;
    bytes.push(128);
    while (bytes.length % 64 !== 56) bytes.push(0);
    for (i = 7; i >= 0; i--) bytes.push(i > 3 ? 0 : (bits >>> (i * 8)) & 255);
    for (i = 0; i < bytes.length; i += 64) {
        var w = [];
        for (j = 0; j < 16; j++) w[j] = (bytes[i + j * 4] << 24) | (bytes[i + j * 4 + 1] << 16) | (bytes[i + j * 4 + 2] << 8) | bytes[i + j * 4 + 3];
        for (j = 16; j < 64; j++) {
            var s0 = rotate(w[j - 15], 7) ^ rotate(w[j - 15], 18) ^ (w[j - 15] >>> 3);
            var s1 = rotate(w[j - 2], 17) ^ rotate(w[j - 2], 19) ^ (w[j - 2] >>> 10);
            w[j] = (w[j - 16] + s0 + w[j - 7] + s1) | 0;
        }
        var a = h.slice(0);
        for (j = 0; j < 64; j++) {
            var e = a[4], x = a[0];
            var t1 = (a[7] + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & a[5]) ^ (~e & a[6])) + k[j] + w[j]) | 0;
            var t2 = ((rotate(x, 2) ^ rotate(x, 13) ^ rotate(x, 22)) + ((x & a[1]) ^ (x & a[2]) ^ (a[1] & a[2]))) | 0;
            a = [(t1 + t2) | 0, a[0], a[1], a[2], (a[3] + t1) | 0, a[4], a[5], a[6]];
        }
        for (j = 0; j < 8; j++) h[j] = (h[j] + a[j]) | 0;
    }
    var out = '';
    for (i = 0; i < 8; i++) out += ('00000000' + (h[i] >>> 0).toString(16)).slice(-8);
    return out;
}
function baseplate(experience) {
    var plate = RULES.baseplate;
    return {id:'00000000-0000-4000-8000-' + experience.replace(/-/g, '').slice(-12), kind:'cube', name:plate.name, color:RULES.part.color,
        x:0, y:-plate.height / 2, z:0, size:plate.width, sizeX:plate.width, sizeY:plate.height, sizeZ:plate.width,
        rotationX:0, rotationY:0, rotationZ:0, material:'plastic', transparency:0, reflectance:0,
        anchored:true, canCollide:true, locked:plate.locked, parent:'Workspace'};
}
function lighting(value) {
    var rules = RULES.lighting, source = value || {};
    function span(v, limits) { return typeof v === 'number' && isFinite(v) ? Math.min(limits.maximum, Math.max(limits.minimum, v)) : limits.standard; }
    function color(v, fallback) { return typeof v === 'string' && /^[0-9A-Fa-f]{6}$/.test(v) ? v : fallback; }
    return {clockTime:span(source.clockTime, rules.clockTime), brightness:span(source.brightness, rules.brightness), fogEnd:span(source.fogEnd, rules.fogEnd),
        ambient:color(source.ambient, rules.ambient), skyTop:color(source.skyTop, rules.skyTop), skyHorizon:color(source.skyHorizon, rules.skyHorizon)};
}
function migrate(w) {
    if (!w) return null;
    if (!w.lighting) w.lighting = lighting(null);
    if (!w.terrainDigest) w.terrainDigest = terrainDigest(terrain(null));
    w.schemaVersion = w.schemaVersion || 1;
    if (w.schemaVersion < 3) {
        if (!w.blocks.some(function(b) { return b.name === RULES.baseplate.name; })) w.blocks.unshift(baseplate(w.id));
        w.genre = RULES.defaultGenre;
        w.serverSize = RULES.serverSize.standard;
        w.createdAt = w.createdAt || null;
        w.revision = w.revision || 0;
        w.schemaVersion = 3;
    }
    if (w.schemaVersion >= RULES.lim.schemaVersion && w.schemaVersion < RULES.schemaVersion) w.schemaVersion = RULES.schemaVersion;
    return w;
}
function summary(w) {
    var s = {id:w.id, title:w.title, owner:w.owner, author:w.author, genre:w.genre, serverSize:w.serverSize, createdAt:w.createdAt, updatedAt:w.updatedAt, revision:w.revision};
    if (image(w.icon)) s.icon = w.icon;
    if (image(w.thumbnail)) s.thumbnail = w.thumbnail;
    return s;
}
function genre(value) {
    if (RULES.genres.indexOf(value) < 0) fail('Genere non valido');
    return value;
}
function capacity(value) {
    if (typeof value !== 'number' || Math.floor(value) !== value || value < RULES.serverSize.minimum || value > RULES.serverSize.maximum) fail('Dimensione del server non valida');
    return value;
}
function part(b, ids) {
    var limits = RULES.world;
    if (!IDENTIFIER.test(b.id) || ids[b.id]) fail('Identificatore oggetto non valido');
    ids[b.id] = true;
    if (RULES.kinds.indexOf(b.kind) < 0 || !/^[0-9A-Fa-f]{6}$/.test(b.color)) fail('Oggetto non valido');
    ['x', 'y', 'z', 'size'].forEach(function(k) { if (typeof b[k] !== 'number' || !isFinite(b[k])) fail('Dimensioni non valide'); });
    if (Math.abs(b.x) > limits.horizontal || Math.abs(b.z) > limits.horizontal || Math.abs(b.y) > limits.vertical) fail('Oggetto fuori dai limiti');
    function span(value, fallback) {
        var n = typeof value === 'number' && isFinite(value) ? value : fallback;
        if (n < limits.minimumSize || n > limits.maximumSize) fail('Oggetto fuori dai limiti');
        return n;
    }
    function angle(value) {
        var n = typeof value === 'number' && isFinite(value) ? value : 0;
        if (n < -180 || n > 180) fail('Orientamento non valido');
        return n;
    }
    function unit(value) {
        var n = typeof value === 'number' && isFinite(value) ? value : 0;
        return n < 0 ? 0 : n > 1 ? 1 : n;
    }
    var result = {id:b.id, kind:b.kind, color:b.color, x:b.x, y:b.y, z:b.z,
        size:span(b.size, 1), sizeX:span(b.sizeX, b.size), sizeY:span(b.sizeY, b.size), sizeZ:span(b.sizeZ, b.size),
        rotationX:angle(b.rotationX), rotationY:angle(b.rotationY), rotationZ:angle(b.rotationZ),
        material:RULES.materials.indexOf(b.material) < 0 ? 'plastic' : b.material,
        transparency:unit(b.transparency), reflectance:unit(b.reflectance),
        anchored:b.anchored !== false, canCollide:b.canCollide !== false, locked:b.locked === true,
        parent:typeof b.parent === 'string' && b.parent.length ? b.parent : 'Workspace',
        name:typeof b.name === 'string' ? b.name.slice(0, limits.nameCharacters) : ''};
    var extra = properties(blockClass(b.kind), b.properties);
    if (Object.keys(extra).length) result.properties = extra;
    if (b.kind === 'mesh') {
        if (typeof b.meshAsset !== 'string' || !ASSET.test(b.meshAsset)) fail('Modello senza riferimento asset');
        result.meshAsset = b.meshAsset;
    }
    return result;
}
function blockClass(kind) {
    var classes = RULES.registry.classes;
    for (var name in classes) if (classes[name].kind === kind) return name;
    fail('Oggetto non valido');
}
function properties(className, raw) {
    var known = RULES.registry.classes[className], result = {};
    if (raw === undefined || raw === null) return result;
    if (typeof raw !== 'object' || Array.isArray(raw) || !known) fail('Proprietà non valide');
    function numbers(list, count) { return Array.isArray(list) && list.length === count && list.every(function(n) { return typeof n === 'number' && isFinite(n); }); }
    Object.keys(raw).forEach(function(name) {
        var type = known.properties[name], v = raw[name];
        if (!type && !(name === 'PrimaryPart' && className === 'Model')) fail('Proprietà ' + name + ' non valida per ' + className);
        var category = type ? type[0] : 'Class', typeName = type ? type[1] : 'BasePart';
        if (category === 'Primitive' && typeName === 'bool') { if (typeof v !== 'boolean') fail('Valore non valido per ' + name); }
        else if ((category === 'Primitive' && typeName === 'string') || typeName === 'ProtectedString') {
            var limit = name === 'Source' ? RULES.instances.sourceCharacters : RULES.instances.textCharacters;
            if (!v || typeof v.String !== 'string' || v.String.length > limit) fail('Testo non valido per ' + name);
        }
        else if (category === 'Primitive') { if (typeof v !== 'number' || !isFinite(v)) fail('Numero non valido per ' + name); }
        else if (typeName === 'Content') { if (!v || typeof v.Content !== 'string' || v.Content.length > RULES.instances.textCharacters) fail('Contenuto non valido per ' + name); }
        else if (typeName === 'Vector3') { if (!v || !numbers(v.Vector3, 3)) fail('Vector3 non valido per ' + name); }
        else if (typeName === 'Vector2') { if (!v || !numbers(v.Vector2, 2)) fail('Vector2 non valido per ' + name); }
        else if (typeName === 'UDim') { if (!v || !numbers(v.UDim, 2)) fail('UDim non valido per ' + name); }
        else if (typeName === 'UDim2') { if (!v || !numbers(v.UDim2, 4)) fail('UDim2 non valido per ' + name); }
        else if (typeName === 'Color3' || typeName === 'BrickColor') { if (!v || typeof v.Color3 !== 'string' || !/^[0-9A-F]{6}$/.test(v.Color3)) fail('Colore non valido per ' + name); }
        else if (category === 'Enum') { if (!v || (RULES.registry.enums[typeName] || []).indexOf(v.Enum) < 0) fail('Valore ' + typeName + ' non valido per ' + name); }
        else if (category === 'Class') { if (!v || !(v.Ref === null || IDENTIFIER.test(v.Ref))) fail('Riferimento non valido per ' + name); }
        else fail('Proprietà ' + name + ' non supportata');
        result[name] = v;
    });
    return result;
}
function instances(list, ids) {
    if (list === undefined || list === null) return [];
    if (!Array.isArray(list) || list.length > RULES.instances.maximum) fail('Massimo ' + RULES.instances.maximum + ' istanze per esperienza');
    return list.map(function(r) {
        if (!r || typeof r !== 'object' || !IDENTIFIER.test(r.id) || ids[r.id]) fail('Identificatore istanza non valido');
        ids[r.id] = true;
        var known = RULES.registry.classes[r.className];
        if (!known || known.kind) fail('Classe non supportata: ' + r.className);
        if (typeof r.name !== 'string' || !r.name.length || r.name.length > RULES.instances.nameCharacters) fail('Nome istanza non valido');
        if (typeof r.parent !== 'string' || !r.parent.length) fail('Genitore istanza non valido');
        var result = {id:r.id, className:r.className, name:r.name, parent:r.parent};
        var values = properties(r.className, r.properties);
        if (Object.keys(values).length) result.properties = values;
        return result;
    });
}
function hierarchy(parts, records) {
    var nodes = {};
    RULES.registry.services.forEach(function(name) { nodes[name] = {className:name, parent:null}; });
    RULES.registry.virtual.forEach(function(entry) { nodes[entry.id] = {className:entry.className, parent:null}; });
    parts.forEach(function(b) { nodes[b.id] = {className:blockClass(b.kind), parent:b.parent}; });
    records.forEach(function(r) { nodes[r.id] = {className:r.className, parent:r.parent}; });
    Object.keys(nodes).forEach(function(id) {
        var node = nodes[id];
        if (node.parent === null) return;
        var owner = nodes[node.parent];
        if (!owner) fail('Genitore non valido: ' + node.parent);
        if (RULES.registry.classes[node.className].parents.indexOf(owner.className) < 0) fail(node.className + ' non può essere inserito in ' + owner.className);
        var parent = node.parent, steps = 0;
        while (nodes[parent] && nodes[parent].parent !== null) {
            parent = nodes[parent].parent;
            if (parent === id || ++steps > parts.length + records.length) fail('Gerarchia ciclica');
        }
    });
}
function chunks(text, size) {
    var parts = [];
    for (var i = 0; i < text.length; i += size) parts.push(text.slice(i, i + size));
    return parts;
}
function legacyScripts(w, prefix) {
    var code;
    if (prefix === 'Published') code = value(read(w.owner, ['PublishedScripts_' + w.id]), 'PublishedScripts_' + w.id, null);
    else {
        var key = 'Studio_' + w.id.replace(/-/g, '');
        var draft = (server.GetUserData({PlayFabId:w.owner, Keys:[key]}).Data || {})[key];
        code = draft ? JSON.parse(draft.Value) : null;
    }
    var parts = code && code.parts && typeof code.parts === 'object' ? code.parts : {};
    var result = [];
    w.blocks.forEach(function(b) {
        var list = parts[b.id] && Array.isArray(parts[b.id].scripts) ? parts[b.id].scripts : [];
        list.forEach(function(script) {
            if (!script || !IDENTIFIER.test(script.id) || typeof script.source !== 'string') return;
            result.push({id:script.id, className:'Script', name:typeof script.name === 'string' && script.name.length ? script.name.slice(0, RULES.instances.nameCharacters) : 'Script', parent:b.id,
                properties:{Disabled:false, Source:{String:script.source.slice(0, RULES.instances.sourceCharacters)}}});
        });
    });
    return result;
}
var BASE64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function unbase64(text) {
    if (typeof text !== 'string' || text.length === 0 || text.length % 4 !== 0 || !/^[A-Za-z0-9+\/]+={0,2}$/.test(text)) fail('Terreno non valido');
    var bytes = [];
    for (var i = 0; i < text.length; i += 4) {
        var n = [0, 1, 2, 3].map(function(o) { var c = text.charAt(i + o); return c === '=' ? 0 : BASE64.indexOf(c); });
        var triple = (n[0] << 18) | (n[1] << 12) | (n[2] << 6) | n[3];
        bytes.push((triple >> 16) & 255);
        if (text.charAt(i + 2) !== '=') bytes.push((triple >> 8) & 255);
        if (text.charAt(i + 3) !== '=') bytes.push(triple & 255);
    }
    return bytes;
}
function tobase64(bytes) {
    var out = '';
    for (var i = 0; i < bytes.length; i += 3) {
        var a = bytes[i], b = i + 1 < bytes.length ? bytes[i + 1] : 0, c = i + 2 < bytes.length ? bytes[i + 2] : 0;
        var triple = (a << 16) | (b << 8) | c;
        out += BASE64.charAt((triple >> 18) & 63) + BASE64.charAt((triple >> 12) & 63);
        out += i + 1 < bytes.length ? BASE64.charAt((triple >> 6) & 63) : '=';
        out += i + 2 < bytes.length ? BASE64.charAt(triple & 63) : '=';
    }
    return out;
}
function terrainVolume() { return Math.pow(RULES.terrain.chunk, 3); }
function terrainLegacy(cell) {
    var material = cell >> 4, level = cell & 15, water = RULES.terrain.materials.indexOf('water');
    return material === water ? level << 8 : (material << 4) | level;
}
function terrainCells(text, format) {
    var runs = unbase64(text), cells = [], width = format >= 2 ? 3 : 2, water = RULES.terrain.materials.indexOf('water');
    if (runs.length === 0 || runs.length % width !== 0) fail('Terreno non valido');
    for (var i = 0; i < runs.length; i += width) {
        var count = runs[i], cell;
        if (width === 3) {
            cell = runs[i + 1] | (runs[i + 2] << 8);
            var raw = (cell >> 4) & 7;
            if ((cell & 0xF080) !== 0 || raw >= water || (raw === 0) !== ((cell & 15) === 0)) fail('Terreno non valido');
        } else {
            var material = runs[i + 1] >> 4, level = runs[i + 1] & 15;
            if (material >= RULES.terrain.materials.length || (material === 0) !== (level === 0)) fail('Terreno non valido');
            cell = terrainLegacy(runs[i + 1]);
        }
        if (count < 1) fail('Terreno non valido');
        for (var k = 0; k < count; k++) cells.push(cell);
    }
    if (cells.length !== terrainVolume()) fail('Terreno non valido');
    return cells;
}
function terrainRuns(cells) {
    var runs = [];
    for (var i = 0; i < cells.length;) {
        var count = 1;
        while (i + count < cells.length && cells[i + count] === cells[i] && count < 255) count++;
        runs.push(count, cells[i] & 255, cells[i] >> 8);
        i += count;
    }
    return tobase64(runs);
}
function terrainWater(value) {
    var rules = RULES.terrain.water, result = {color:rules.color, transparency:rules.transparency.standard, reflectance:rules.reflectance.standard, waveSize:rules.waveSize.standard, waveSpeed:rules.waveSpeed.standard};
    if (value === null || value === undefined) return result;
    if (typeof value !== 'object' || Array.isArray(value)) fail('Terreno non valido');
    if (value.color !== undefined && value.color !== null) {
        if (typeof value.color !== 'string' || !/^[0-9A-Fa-f]{6}$/.test(value.color)) fail('Terreno non valido');
        result.color = value.color.toUpperCase();
    }
    ['transparency', 'reflectance', 'waveSize', 'waveSpeed'].forEach(function(key) {
        var v = value[key];
        if (v === undefined || v === null) return;
        if (typeof v !== 'number' || !isFinite(v) || v < rules[key].minimum || v > rules[key].maximum) fail('Terreno non valido');
        result[key] = v;
    });
    return result;
}
function terrain(value) {
    var t = RULES.terrain, result = {format:t.format, settings:null, water:terrainWater(null), chunks:{}};
    if (!value) return result;
    if (typeof value !== 'object' || (value.chunks && typeof value.chunks !== 'object')) fail('Terreno non valido');
    var format = 1;
    if (value.format !== undefined && value.format !== null) {
        if (value.format !== t.format) fail('Terreno non valido');
        format = t.format;
    }
    result.water = terrainWater(value.water);
    var s = value.settings;
    if (s && typeof s === 'object') {
        function bounded(v, low, high) { if (typeof v !== 'number' || !isFinite(v) || v < low || v > high) fail('Terreno non valido'); return v; }
        result.settings = {seed:Math.floor(bounded(s.seed, 1, 999999)), amplitude:bounded(s.amplitude, 0, 12), baseHeight:bounded(s.baseHeight, -16, 16), waterLevel:bounded(s.waterLevel, -16, 16)};
    }
    var counts = [t.cellsX / t.chunk, t.cellsY / t.chunk, t.cellsZ / t.chunk];
    Object.keys(value.chunks || {}).sort().forEach(function(key) {
        var parts = key.split(',');
        if (parts.length !== 3 || parts.some(function(p, axis) { return !/^\d+$/.test(p) || +p >= counts[axis]; })) fail('Terreno non valido');
        if (typeof value.chunks[key] !== 'string') fail('Terreno non valido');
        var cells = terrainCells(value.chunks[key], format);
        if (cells.some(function(c) { return c !== 0; })) result.chunks[key] = terrainRuns(cells);
    });
    return result;
}
function terrainText(t) {
    var keys = Object.keys(t.chunks).sort(), chunks = keys.map(function(k) { return JSON.stringify(k) + ':' + JSON.stringify(t.chunks[k]); });
    return '{"format":' + JSON.stringify(t.format) + ',"settings":' + JSON.stringify(t.settings) + ',"water":' + JSON.stringify(t.water) + ',"chunks":{' + chunks.join(',') + '}}';
}
function terrainDigest(t) { return sha256(terrainText(t)).slice(0, 16); }
function terrainSplit(text) {
    var parts = [];
    for (var i = 0; i < text.length; i += RULES.terrain.keyCharacters) parts.push(text.slice(i, i + RULES.terrain.keyCharacters));
    return parts;
}
function terrainWaterParts(w) {
    var t = RULES.terrain, cs = t.cellSize, origin = [-t.cellsX * cs / 2, t.bottom, -t.cellsZ * cs / 2];
    var water = w.blocks.filter(function(b) { return b.material === 'water'; });
    if (!water.length) return;
    var decoded = {};
    Object.keys(w.terrain.chunks).forEach(function(k) { decoded[k] = terrainCells(w.terrain.chunks[k], RULES.terrain.format); });
    water.forEach(function(b) {
        var half = [(b.sizeX || b.size) / 2, (b.sizeY || b.size) / 2, (b.sizeZ || b.size) / 2];
        for (var i = 0; i < t.cellsX; i++) for (var j = 0; j < t.cellsY; j++) for (var k = 0; k < t.cellsZ; k++) {
            var c = [origin[0] + (i + 0.5) * cs, origin[1] + (j + 0.5) * cs, origin[2] + (k + 0.5) * cs];
            if (Math.abs(c[0] - b.x) > half[0] || Math.abs(c[1] - b.y) > half[1] || Math.abs(c[2] - b.z) > half[2]) continue;
            var key = Math.floor(i / t.chunk) + ',' + Math.floor(j / t.chunk) + ',' + Math.floor(k / t.chunk);
            var cells = decoded[key] || (decoded[key] = []);
            if (!cells.length) for (var n = 0; n < terrainVolume(); n++) cells.push(0);
            var offset = ((i % t.chunk) * t.chunk + (j % t.chunk)) * t.chunk + (k % t.chunk);
            if ((cells[offset] & 15) === 0) cells[offset] = t.levels << 8;
        }
    });
    Object.keys(decoded).forEach(function(k) { if (decoded[k].some(function(c) { return c !== 0; })) w.terrain.chunks[k] = terrainRuns(decoded[k]); });
    w.blocks = w.blocks.filter(function(b) { return b.material !== 'water'; });
    w.terrainDigest = terrainDigest(w.terrain);
}
function attach(owner, w, prefix) {
    if (!w) return null;
    var count = w.terrainParts || 0, objects = w.instanceParts || 0, keys = [], records = [];
    for (var k = 0; k < count; k++) keys.push(prefix + 'Terrain_' + w.id + '_' + k);
    for (var n = 0; n < objects; n++) records.push(prefix + 'Instances_' + w.id + '_' + n);
    var data = count + objects ? read(owner, keys.concat(records)) : {};
    var text = keys.map(function(key) { return value(data, key, ''); }).join('');
    w.terrain = text ? terrain(JSON.parse(text)) : terrain(null);
    terrainWaterParts(w);
    var listed = records.map(function(key) { return value(data, key, ''); }).join('');
    if (w.schemaVersion < 4) { w.instances = legacyScripts(w, prefix); w.schemaVersion = 4; }
    else w.instances = listed ? JSON.parse(listed) : [];
    delete w.terrainParts;
    delete w.instanceParts;
    return w;
}
function intact(w) {
    return !!w && typeof w === 'object' && typeof w.id === 'string' && Array.isArray(w.blocks) && w.blocks.every(function(b) { return !!b && typeof b === 'object'; });
}
function stored(owner, id, prefix) {
    var raw;
    try { raw = value(read(owner, [prefix + id]), prefix + id, null); } catch (error) { refuse('WorldCorrupt', 'Dati dell’esperienza danneggiati'); }
    if (raw && !intact(raw)) refuse('WorldCorrupt', 'Dati dell’esperienza danneggiati');
    return migrate(raw);
}
function listing() {
    var result = server.GetSharedGroupData({SharedGroupId:'LudisseyPublished'}).Data || {};
    return Object.keys(result).map(function(k) { return JSON.parse(result[k].Value); }).map(function(entry) {
        return {id:entry.id, title:entry.title, owner:entry.owner, author:entry.author,
            genre:entry.genre || RULES.defaultGenre, serverSize:entry.serverSize || RULES.serverSize.standard,
            createdAt:entry.createdAt || null, updatedAt:entry.updatedAt || null, revision:entry.revision || 0, icon:entry.icon, thumbnail:entry.thumbnail};
    });
}
function published(id) {
    var data = server.GetSharedGroupData({SharedGroupId:'LudisseyPublished', Keys:[id]}).Data || {};
    return data[id] ? JSON.parse(data[id].Value) : null;
}
function content(w) { return JSON.stringify([w.title, w.blocks, w.genre, w.serverSize, w.lighting, w.terrainDigest, w.instancesDigest, 'languageVersion' in w ? w.languageVersion : RULES.lim.languageVersion, 'engineApiVersion' in w ? w.engineApiVersion : RULES.lim.engineApiVersion, typeof w.icon === 'string' ? w.icon : null, typeof w.thumbnail === 'string' ? w.thumbnail : null]); }
function persist(args, publish) {
    var p = active();
    var w = args.world;
    if (!w || !IDENTIFIER.test(w.id)) fail('Esperienza non valida');
    var entry = published(w.id);
    if (entry && entry.owner !== currentPlayerId) fail('Non puoi modificare esperienze di altri giocatori');
    if (!Array.isArray(w.blocks) || w.blocks.length > RULES.world.parts) fail('Massimo ' + RULES.world.parts + ' oggetti per esperienza');
    var ids = {};
    var parts = w.blocks.map(function(b) { return part(b, ids); });
    var ground = terrain(w.terrain);
    var groundParts = terrainSplit(terrainText(ground));
    if (groundParts.length > RULES.terrain.keys) fail('Terreno troppo grande: riduci le aree modificate');
    var records = instances(w.instances, ids);
    var objectText = JSON.stringify(records);
    var objectParts = records.length ? chunks(objectText, RULES.instances.keyCharacters) : [];
    if (objectParts.length > RULES.instances.keys) fail('Script e istanze superano lo spazio disponibile');
    hierarchy(parts, records);
    var previous = stored(currentPlayerId, w.id, 'World_');
    var moment = iso(now());
    var language = 'languageVersion' in w ? w.languageVersion : RULES.lim.languageVersion;
    if (typeof language !== 'string' || RULES.lim.languageVersions.indexOf(language) < 0) refuse('VersionMismatch', 'Questa esperienza richiede una versione più nuova di Ludissey. Aggiorna l’app.');
    var api = 'engineApiVersion' in w ? w.engineApiVersion : RULES.lim.engineApiVersion;
    if (typeof api !== 'number' || Math.floor(api) !== api || api < 1 || api > RULES.lim.engineApiVersion) refuse('VersionMismatch', 'Questa esperienza richiede una versione più nuova di Ludissey. Aggiorna l’app.');
    var result = {id:w.id, schemaVersion:typeof w.schemaVersion === 'number' && w.schemaVersion >= RULES.lim.schemaVersion && w.schemaVersion <= RULES.schemaVersion ? RULES.schemaVersion : 4, languageVersion:language, engineApiVersion:api, title:text(w.title, RULES.world.titleCharacters),
        blocks:parts, owner:currentPlayerId, author:p.username,
        genre:genre(w.genre), serverSize:capacity(w.serverSize), lighting:lighting(w.lighting), terrainDigest:terrainDigest(ground), instancesDigest:sha256(objectText).slice(0, 16),
        createdAt:previous ? previous.createdAt : moment, updatedAt:previous ? previous.updatedAt : moment, revision:previous ? previous.revision : 0};
    if (image(w.icon)) result.icon = w.icon;
    if (image(w.thumbnail)) result.thumbnail = w.thumbnail;
    if (!previous || publish || content(previous) !== content(result)) { result.updatedAt = moment; result.revision = (previous ? previous.revision : 0) + 1; }
    if (JSON.stringify(result).length > RULES.world.payloadCharacters) fail('Esperienza troppo grande: riduci gli oggetti');
    if (publish) {
        var files = uploaded(account(currentPlayerId));
        references(result, records).forEach(function(asset) { if (!files[asset]) refuse('AssetMissing', 'Risorsa non caricata: ' + asset); });
    }
    var index = value(read(currentPlayerId, ['LudisseyWorlds']), 'LudisseyWorlds', []);
    if (index.indexOf(result.id) < 0) {
        if (index.length >= RULES.world.experiencesPerDeveloper) fail('Massimo ' + RULES.world.experiencesPerDeveloper + ' esperienze');
        index.push(result.id);
    }
    var document = JSON.parse(JSON.stringify(result));
    document.terrainParts = groundParts.length;
    document.instanceParts = objectParts.length;
    function store(prefix, extra) {
        var data = extra;
        data[prefix + 'World_' + result.id] = document;
        groundParts.forEach(function(part, k) { data[prefix + 'Terrain_' + result.id + '_' + k] = part; });
        objectParts.forEach(function(part, k) { data[prefix + 'Instances_' + result.id + '_' + k] = part; });
        save(currentPlayerId, data);
        var stale = prefix === 'Published' ? ['PublishedScripts_' + result.id] : [];
        for (var k = groundParts.length; k < RULES.terrain.keys; k++) stale.push(prefix + 'Terrain_' + result.id + '_' + k);
        for (var n = objectParts.length; n < RULES.instances.keys; n++) stale.push(prefix + 'Instances_' + result.id + '_' + n);
        server.UpdateUserReadOnlyData({PlayFabId:currentPlayerId, KeysToRemove:stale});
    }
    store('', {LudisseyWorlds:index});
    if (publish) store('Published', {});
    result.terrain = ground;
    result.instances = records;
    if (publish) {
        var listed = {};
        listed[result.id] = JSON.stringify(summary(result));
        server.UpdateSharedGroupData({SharedGroupId:'LudisseyPublished', Data:listed, Permission:'Public'});
    }
    return result;
}
handlers.LudisseySaveWorld = function(args) { return persist(args, false); };
handlers.LudisseyPublishWorld = function(args) { return persist(args, true); };
handlers.LudisseyWorlds = function() {
    active();
    var index = value(read(currentPlayerId, ['LudisseyWorlds']), 'LudisseyWorlds', []);
    if (!index.length) return {worlds:[]};
    var data = read(currentPlayerId, index.map(function(id) { return 'World_' + id; }));
    return {worlds:index.map(function(id) {
        var raw;
        try { raw = value(data, 'World_' + id, null); } catch (error) { return null; }
        return intact(raw) ? attach(currentPlayerId, migrate(raw), '') : null;
    }).filter(function(w) { return !!w; })};
};
handlers.LudisseyDiscover = function() { active(); return {worlds:listing()}; };
handlers.LudisseyUnpublishWorld = function(args) {
    active();
    var index = value(read(currentPlayerId, ['LudisseyWorlds']), 'LudisseyWorlds', []);
    if (typeof args.id !== 'string' || index.indexOf(args.id) < 0) fail('Esperienza non trovata');
    var entry = published(args.id);
    if (!entry) return {published:false};
    if (entry.owner !== currentPlayerId) fail('Non puoi modificare esperienze di altri giocatori');
    server.UpdateSharedGroupData({SharedGroupId:'LudisseyPublished', KeysToRemove:[args.id]});
    return {published:false};
};
handlers.LudisseyDeleteWorld = function(args) {
    active();
    var index = value(read(currentPlayerId, ['LudisseyWorlds']), 'LudisseyWorlds', []);
    if (index.indexOf(args.id) < 0) fail('Esperienza non trovata');
    var entry = published(args.id);
    if (entry && entry.owner !== currentPlayerId) fail('Non puoi eliminare esperienze di altri giocatori');
    server.UpdateSharedGroupData({SharedGroupId:'LudisseyPublished', KeysToRemove:[args.id]});
    var removed = ['World_' + args.id, 'PublishedWorld_' + args.id, 'PublishedScripts_' + args.id];
    for (var k = 0; k < RULES.terrain.keys; k++) removed.push('Terrain_' + args.id + '_' + k, 'PublishedTerrain_' + args.id + '_' + k);
    for (var n = 0; n < RULES.instances.keys; n++) removed.push('Instances_' + args.id + '_' + n, 'PublishedInstances_' + args.id + '_' + n);
    server.UpdateUserReadOnlyData({PlayFabId:currentPlayerId, KeysToRemove:removed});
    save(currentPlayerId, {LudisseyWorlds:index.filter(function(id) { return id !== args.id; })});
    return {deleted:true};
};
function experience(id) {
    if (typeof id !== 'string' || !IDENTIFIER.test(id)) refuse('ExperienceUnavailable', 'Esperienza non valida');
    var entry = published(id);
    var own = value(read(currentPlayerId, ['LudisseyWorlds']), 'LudisseyWorlds', []).indexOf(id) >= 0;
    var owner = entry ? entry.owner : own ? currentPlayerId : null;
    if (!owner) refuse('ExperienceUnavailable', 'Esperienza non disponibile');
    var live = entry ? stored(owner, id, 'PublishedWorld_') : null;
    var w = live ? attach(owner, live, 'Published') : attach(owner, stored(owner, id, 'World_'), '');
    if (!w) refuse('ExperienceUnavailable', 'Esperienza rimossa');
    w.published = !!entry;
    return w;
}
function account(player) {
    var info = server.GetUserAccountInfo({PlayFabId:player}).UserInfo;
    var title = info && info.TitleInfo && info.TitleInfo.TitlePlayerAccount;
    if (!title) refuse('ExperienceUnavailable', 'Account non disponibile');
    return {Id:title.Id, Type:title.Type};
}
function uploaded(key) { return entity.GetFiles({Entity:key}).Metadata || {}; }
function references(w, records) {
    var found = {};
    w.blocks.forEach(function(b) { if (b.meshAsset) found[b.meshAsset] = true; });
    [w.icon, w.thumbnail].forEach(function(asset) { if (image(asset)) found[asset] = true; });
    (records || w.instances || []).forEach(function(r) {
        Object.keys(r.properties || {}).forEach(function(name) {
            var v = r.properties[name];
            if (!v || typeof v.Content !== 'string' || v.Content.indexOf(RULES.assets.prefix) !== 0) return;
            var asset = v.Content.slice(RULES.assets.prefix.length);
            if (ASSET.test(asset)) found[asset] = true;
        });
    });
    return Object.keys(found).sort();
}
function assets(args) {
    if (!Array.isArray(args.assets) || args.assets.length < 1 || args.assets.length > RULES.world.parts) refuse('AssetRejected', 'Elenco risorse non valido');
    args.assets.forEach(function(asset) { if (typeof asset !== 'string' || !ASSET.test(asset)) refuse('AssetRejected', 'Identificatore risorsa non valido'); });
    return args.assets;
}
handlers.LudisseyAssetUpload = function(args) {
    active();
    var requested = assets(args);
    var key = account(currentPlayerId);
    var files = uploaded(key);
    var pending = requested.filter(function(asset) { return !files[asset]; });
    if (!pending.length) return {pending:[]};
    var response = entity.InitiateFileUploads({Entity:key, FileNames:pending});
    return {pending:(response.UploadDetails || []).map(function(detail) { return {id:detail.FileName, url:detail.UploadUrl}; })};
};
handlers.LudisseyAssetFinalize = function(args) {
    active();
    var requested = assets(args);
    entity.FinalizeFileUploads({Entity:account(currentPlayerId), FileNames:requested});
    return {finalized:requested};
};
handlers.LudisseyAssetResolve = function(args) {
    active();
    var w = experience(args.experienceId);
    var requested = assets(args);
    var allowed = references(w);
    var files = uploaded(account(w.owner));
    var urls = {};
    requested.forEach(function(asset) {
        if (allowed.indexOf(asset) < 0) refuse('AssetRejected', 'Risorsa non usata da questa esperienza');
        if (!files[asset]) refuse('AssetMissing', 'Risorsa non disponibile: ' + asset);
        urls[asset] = files[asset].DownloadUrl;
    });
    return {urls:urls};
};
handlers.LudisseyPortraits = function(args) {
    active();
    if (args.save) {
        var own = uploaded(account(currentPlayerId));
        ['bust', 'figure'].forEach(function(kind) { if (!image(args.save[kind]) || !own[args.save[kind]]) refuse('AssetRejected', 'Immagine dell’avatar non valida'); });
        save(currentPlayerId, {LudisseyPortraits:{bust:args.save.bust, figure:args.save.figure}});
    }
    var players = {};
    (Array.isArray(args.players) ? args.players : []).slice(0, RULES.account.portraitPlayers).forEach(function(id) {
        if (typeof id !== 'string' || id.length < 1 || id.length > 32) return;
        var stored = null, owner = null;
        try {
            owner = profile(id);
            stored = owner ? value(read(id, ['LudisseyPortraits']), 'LudisseyPortraits', null) : null;
        } catch (missing) {
            owner = null;
        }
        if (!owner) return;
        players[id] = {gender:owner.gender === 'female' ? 'female' : 'male'};
        if (!stored) return;
        var files = uploaded(account(id));
        if (!files[stored.bust] || !files[stored.figure]) return;
        players[id] = {gender:players[id].gender, bust:stored.bust, bustUrl:files[stored.bust].DownloadUrl, figure:stored.figure, figureUrl:files[stored.figure].DownloadUrl};
    });
    return {players:players};
};
var live = {
    key: function(w) { return account(w.owner); },
    name: function(w) { return w.id.replace(/-/g, ''); },
    load: function(w, key) {
        var response = entity.GetObjects({Entity:key});
        var object = (response.Objects || {})[live.name(w)];
        return {record:object ? object.DataObject : null, version:response.ProfileVersion};
    },
    commit: function(w, key, record, version) {
        try {
            entity.SetObjects({Entity:key, ExpectedProfileVersion:version, Objects:[{ObjectName:live.name(w), DataObject:record}]});
            return true;
        } catch (error) {
            if (JSON.stringify(error).indexOf('VersionMismatch') >= 0) return false;
            throw error;
        }
    }
};
function normalize(record) {
    record = record || {};
    return {visits:record.visits || 0, servers:record.servers || []};
}
function occupancy(s) { return s.players.length + s.reservations.length; }
function sweep(record, moment) {
    record.servers.forEach(function(s) {
        s.reservations = s.reservations.filter(function(r) { return r.expires > moment; });
        s.players = s.players.filter(function(p) { return moment - p.seen < RULES.playerTimeoutSeconds * 1000; });
        if (occupancy(s) > 0) s.emptySince = null;
        else if (s.emptySince === null) s.emptySince = moment;
    });
    record.servers = record.servers.filter(function(s) { return s.emptySince === null || moment - s.emptySince < RULES.emptyServerSeconds * 1000; });
}
function transact(w, mutate) {
    var key = live.key(w);
    for (var attempt = 0; attempt < RULES.transactionAttempts; attempt++) {
        var state = live.load(w, key);
        var record = normalize(state.record);
        var moment = now();
        sweep(record, moment);
        var result = mutate(record, moment);
        if (JSON.stringify(record).length > RULES.registryCharacters) refuse('RegistryFull', 'Questa esperienza è piena. Riprova tra poco.');
        if (live.commit(w, key, record, state.version)) return result;
    }
    refuse('ServerBusy', 'Server occupati: riprova tra poco');
}
function observe(w) {
    var record = normalize(live.load(w, live.key(w)).record);
    sweep(record, now());
    return record;
}
function locate(record, session) {
    for (var i = 0; i < record.servers.length; i++) {
        var s = record.servers[i];
        for (var j = 0; j < s.reservations.length; j++) if (s.reservations[j].session === session) return {server:s, reservation:s.reservations[j], index:j};
        for (j = 0; j < s.players.length; j++) if (s.players[j].session === session) return {server:s, player:s.players[j], index:j};
    }
    return {};
}
function release(record, player, moment) {
    var released = [];
    record.servers.forEach(function(s) {
        s.players.forEach(function(p) { if (p.player === player) released.push({server:s.id, session:p.session}); });
        s.reservations = s.reservations.filter(function(r) { return r.player !== player; });
        s.players = s.players.filter(function(p) { return p.player !== player; });
        if (occupancy(s) === 0 && s.emptySince === null) s.emptySince = moment;
    });
    return released;
}
function allocate(record, size, requested, moment) {
    var target;
    if (requested) {
        target = record.servers.filter(function(s) { return s.id === requested; })[0];
        if (!target) refuse('ServerClosed', 'Il server non è più disponibile');
        if (occupancy(target) >= target.capacity) refuse('ServerFull', 'Il server è pieno');
        return target;
    }
    target = record.servers.filter(function(s) { return occupancy(s) < s.capacity; })[0];
    if (target) return target;
    if (record.servers.length >= RULES.maxServers) refuse('ExperienceFull', 'Tutti i server sono pieni');
    var used = record.servers.map(function(s) { return s.sequence; });
    var sequence = 1;
    while (used.indexOf(sequence) >= 0) sequence++;
    target = {id:identifier(), sequence:sequence, capacity:size, createdAt:moment, emptySince:null, players:[], reservations:[], chat:[], chatSequence:0};
    record.servers.push(target);
    record.servers.sort(function(a, b) { return a.sequence - b.sequence; });
    return target;
}
function presence(id) { return 'LudisseyPresence_' + id; }
function join_presence(serverId, player) {
    try { server.CreateSharedGroup({SharedGroupId:presence(serverId)}); } catch (error) { if (JSON.stringify(error).indexOf('SharedGroupId') < 0 && JSON.stringify(error).indexOf('Exists') < 0) throw error; }
    server.AddSharedGroupMembers({SharedGroupId:presence(serverId), PlayFabIds:[player]});
}
function leave_presence(serverId, player, session) {
    try {
        server.UpdateSharedGroupData({SharedGroupId:presence(serverId), KeysToRemove:[session]});
        server.RemoveSharedGroupMembers({SharedGroupId:presence(serverId), PlayFabIds:[player]});
    } catch (error) {}
}
function lifecycle(s) {
    var states = RULES.serverStates;
    if (s.players.length === 0) {
        if (s.reservations.length === 0) return states.draining;
        return s.reservations.some(function(r) { return r.opened; }) ? states.ready : states.provisioning;
    }
    return occupancy(s) >= s.capacity ? states.full : states.joinable;
}
function describe(s) {
    return {id:s.id, sequence:s.sequence, capacity:s.capacity, players:s.players.length, reserved:s.reservations.length, state:lifecycle(s), region:runtime.region, observedAt:iso(now())};
}
function roster(s) { return s.players.map(function(p) { return p.name; }).sort(); }
function listed(s) { return s.players.length > 0; }
function prune_presence(serverId, s) {
    var active = {};
    (s ? s.players : []).forEach(function(p) { active[p.session] = true; });
    try {
        var data = server.GetSharedGroupData({SharedGroupId:presence(serverId)}).Data || {};
        var stale = Object.keys(data).filter(function(key) { return !active[key]; });
        if (stale.length) server.UpdateSharedGroupData({SharedGroupId:presence(serverId), KeysToRemove:stale});
    } catch (error) {}
}
function session(args) {
    if (typeof args.sessionId !== 'string' || !IDENTIFIER.test(args.sessionId)) refuse('SessionRejected', 'Sessione non valida');
    return args.sessionId;
}
function member(record, id) {
    var found = locate(record, id);
    if (!found.player || found.player.player !== currentPlayerId) refuse('SessionEnded', 'Non sei più connesso a questo server');
    return found;
}
function details(w, record) {
    return {experience:{id:w.id, title:w.title, owner:w.owner, author:w.author, genre:w.genre, serverSize:w.serverSize,
        createdAt:w.createdAt, updatedAt:w.updatedAt, revision:w.revision, published:w.published, visits:record.visits},
        servers:record.servers.filter(listed).map(describe)};
}
handlers.LudisseyExperience = function(args) {
    active();
    var w = experience(args.id);
    var result = details(w, observe(w));
    if (args.world === true) result.world = w;
    return result;
};
handlers.LudisseyRequestSession = function(args) {
    var p = active();
    if (args.playerProtocol !== RULES.playerProtocol) refuse('VersionMismatch', 'Versione di Ludissey non compatibile');
    var w = experience(args.experienceId);
    var requested = typeof args.serverId === 'string' && args.serverId ? args.serverId : null;
    var token = runtime.random();
    var released = [];
    var ticket = transact(w, function(record, moment) {
        released = release(record, currentPlayerId, moment);
        var target = allocate(record, w.serverSize, requested, moment);
        var id = identifier();
        var expires = moment + RULES.reservationSeconds * 1000;
        target.reservations.push({session:id, player:currentPlayerId, name:p.username, hash:sha256(token).slice(0, 32), expires:expires, opened:false});
        target.emptySince = null;
        return {sessionId:id, joinTicket:token, expiresAt:Math.floor(expires / 1000), playerProtocol:RULES.playerProtocol,
            nonce:sha256(id + token).slice(0, 32), experienceId:w.id, serverId:target.id, serverSequence:target.sequence};
    });
    released.forEach(function(entry) { leave_presence(entry.server, currentPlayerId, entry.session); });
    return ticket;
};
handlers.LudisseyOpenSession = function(args) {
    active();
    var w = experience(args.experienceId);
    var id = session(args);
    var token = typeof args.joinTicket === 'string' ? args.joinTicket : '';
    var assigned = transact(w, function(record) {
        var found = locate(record, id);
        if (!found.reservation) {
            if (found.player) refuse('SessionUsed', 'Questo accesso è già stato utilizzato');
            refuse('SessionExpired', 'Sessione scaduta');
        }
        var r = found.reservation;
        if (r.player !== currentPlayerId || r.hash !== sha256(token).slice(0, 32)) refuse('SessionRejected', 'Accesso alla sessione rifiutato');
        if (r.opened) refuse('SessionUsed', 'Questo accesso è già stato utilizzato');
        r.opened = true;
        return describe(found.server);
    });
    return {world:w, server:assigned};
};
handlers.LudisseyReadySession = function(args) {
    active();
    var w = experience(args.experienceId);
    var id = session(args);
    if (typeof args.revision === 'number' && args.revision !== w.revision) refuse('WorldChanged', 'L’esperienza è appena stata aggiornata. Riprova.');
    var ready = transact(w, function(record, moment) {
        var found = locate(record, id);
        if (found.player) {
            if (found.player.player !== currentPlayerId) refuse('SessionUsed', 'Questo accesso è già stato utilizzato');
            found.player.seen = moment;
            return {server:describe(found.server), roster:roster(found.server), visits:record.visits, presence:presence(found.server.id), player:currentPlayerId, repeated:true, record:found.server};
        }
        var r = found.reservation;
        if (!r) refuse('SessionExpired', 'Sessione scaduta');
        if (r.player !== currentPlayerId || !r.opened) refuse('SessionRejected', 'Accesso alla sessione rifiutato');
        found.server.reservations.splice(found.index, 1);
        found.server.players.push({session:id, player:currentPlayerId, name:r.name, seen:moment});
        record.visits += 1;
        return {server:describe(found.server), roster:roster(found.server), visits:record.visits, presence:presence(found.server.id), player:currentPlayerId, record:found.server};
    });
    join_presence(ready.server.id, currentPlayerId);
    prune_presence(ready.server.id, ready.record);
    delete ready.record;
    return ready;
};
handlers.LudisseyHeartbeat = function(args) {
    active();
    var w = experience(args.experienceId);
    var id = session(args);
    var beat = transact(w, function(record, moment) {
        var found = member(record, id);
        found.player.seen = moment;
        return {server:describe(found.server), roster:roster(found.server), record:found.server};
    });
    prune_presence(beat.server.id, beat.record);
    delete beat.record;
    return beat;
};
handlers.LudisseyServerState = function(args) {
    active();
    var w = experience(args.experienceId);
    var found = member(observe(w), session(args));
    var after = typeof args.after === 'number' ? args.after : 0;
    return {server:describe(found.server), roster:roster(found.server), chat:found.server.chat.filter(function(m) { return m.sequence > after; })};
};
handlers.LudisseyChat = function(args) {
    active();
    var w = experience(args.experienceId);
    var id = session(args);
    var message = text(args.message, RULES.chatCharacters);
    return transact(w, function(record, moment) {
        var found = member(record, id);
        var recent = (found.player.chatTimes || []).filter(function(t) { return moment - t < RULES.chatWindowSeconds * 1000; });
        if (recent.length >= RULES.chatBurst) refuse('ChatRateLimited', 'Piano! Aspetta qualche secondo prima di scrivere ancora.');
        recent.push(moment);
        found.player.chatTimes = recent;
        found.player.seen = moment;
        var s = found.server;
        s.chatSequence += 1;
        s.chat.push({sequence:s.chatSequence, name:found.player.name, text:message, at:moment});
        if (s.chat.length > RULES.chatMessages) s.chat.splice(0, s.chat.length - RULES.chatMessages);
        return {sequence:s.chatSequence};
    });
};
handlers.LudisseyLeaveSession = function(args) {
    active();
    var w = experience(args.experienceId);
    var id = session(args);
    var outcome = transact(w, function(record, moment) {
        var found = locate(record, id);
        var owned = found.reservation ? found.reservation.player === currentPlayerId : found.player ? found.player.player === currentPlayerId : false;
        if (!owned) return {left:false};
        if (found.reservation) found.server.reservations.splice(found.index, 1);
        else found.server.players.splice(found.index, 1);
        if (occupancy(found.server) === 0) found.server.emptySince = moment;
        return {left:true, server:describe(found.server)};
    });
    if (outcome.left) leave_presence(outcome.server.id, currentPlayerId, id);
    return outcome;
};
handlers.LudisseyServerInfo = function() {
    return {schemaVersion:RULES.schemaVersion, playerProtocol:RULES.playerProtocol, handlers:Object.keys(handlers).sort()};
};
