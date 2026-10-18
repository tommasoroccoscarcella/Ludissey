var emulator = {
    time: 1790000000000,
    serial: 0,
    players: {},
    title: {},
    groups: {},
    entities: {},
    blobs: {},
    members: {},
    interleave: null,
    versioned: true,
    reads: 0,
    conflicts: 0,
    clone: function(value) { return value === undefined ? undefined : JSON.parse(JSON.stringify(value)); },
    player: function(id) {
        var p = emulator.players[id];
        if (!p) throw {apiErrorInfo:{apiError:{error:'AccountNotFound'}}};
        return p;
    },
    register: function(id, username) {
        emulator.players[id] = {username:username, readOnly:{}, user:{}, inventory:[], friends:[], entity:'e' + id};
        emulator.entities['e' + id] = {version:0, objects:{}, files:{}};
        return id;
    },
    random: function() {
        emulator.serial += 1;
        var seed = ('0000000000000000' + emulator.serial.toString(16)).slice(-16);
        return sha256('emulator-' + seed);
    }
};
runtime.clock = function() { return emulator.time; };
runtime.random = function() { return emulator.random(); };
var server = {
    GetTitleData: function(r) {
        var data = {};
        (r.Keys || Object.keys(emulator.title)).forEach(function(k) { if (emulator.title[k] !== undefined) data[k] = emulator.title[k]; });
        return {Data:data};
    },
    GetUserReadOnlyData: function(r) {
        var p = emulator.player(r.PlayFabId), data = {};
        (r.Keys || Object.keys(p.readOnly)).forEach(function(k) { if (p.readOnly[k] !== undefined) data[k] = {Value:p.readOnly[k]}; });
        return {Data:data};
    },
    UpdateUserReadOnlyData: function(r) {
        var p = emulator.player(r.PlayFabId);
        Object.keys(r.Data || {}).forEach(function(k) { p.readOnly[k] = r.Data[k]; });
        (r.KeysToRemove || []).forEach(function(k) { delete p.readOnly[k]; });
        return {};
    },
    GetUserAccountInfo: function(r) {
        var p = emulator.player(r.PlayFabId);
        return {UserInfo:{PlayFabId:r.PlayFabId, Username:p.username, TitleInfo:{DisplayName:p.username, TitlePlayerAccount:{Id:p.entity, Type:'title_player_account'}}}};
    },
    GetUserData: function(r) {
        var p = emulator.player(r.PlayFabId), data = {};
        (r.Keys || Object.keys(p.user)).forEach(function(k) { if (p.user[k] !== undefined) data[k] = {Value:p.user[k]}; });
        return {Data:data};
    },
    GetUserInventory: function(r) { return {Inventory:emulator.player(r.PlayFabId).inventory}; },
    GrantItemsToUser: function(r) {
        var p = emulator.player(r.PlayFabId);
        r.ItemIds.forEach(function(id) { p.inventory.push({ItemId:id, ItemInstanceId:id + '-' + p.inventory.length}); });
        return {};
    },
    GetFriendsList: function(r) {
        return {Friends:emulator.player(r.PlayFabId).friends.map(function(f) {
            var other = emulator.players[f.FriendPlayFabId];
            return other ? {FriendPlayFabId:f.FriendPlayFabId, Username:other.username} : {FriendPlayFabId:f.FriendPlayFabId};
        })};
    },
    AddFriend: function(r) { emulator.player(r.PlayFabId).friends.push({FriendPlayFabId:r.FriendPlayFabId}); return {}; },
    RemoveFriend: function(r) {
        var p = emulator.player(r.PlayFabId);
        p.friends = p.friends.filter(function(f) { return f.FriendPlayFabId !== r.FriendPlayFabId; });
        return {};
    },
    GetSharedGroupData: function(r) {
        var g = emulator.groups[r.SharedGroupId] || {}, data = {};
        (r.Keys || Object.keys(g)).forEach(function(k) { if (g[k] !== undefined) data[k] = {Value:g[k]}; });
        return {Data:data};
    },
    CreateSharedGroup: function(r) {
        if (emulator.groups[r.SharedGroupId]) throw {apiErrorInfo:{apiError:{error:'SharedGroupIdExists'}}};
        emulator.groups[r.SharedGroupId] = {};
        emulator.members[r.SharedGroupId] = [];
        return {SharedGroupId:r.SharedGroupId};
    },
    AddSharedGroupMembers: function(r) {
        var list = emulator.members[r.SharedGroupId] = emulator.members[r.SharedGroupId] || [];
        r.PlayFabIds.forEach(function(id) { if (list.indexOf(id) < 0) list.push(id); });
        return {};
    },
    RemoveSharedGroupMembers: function(r) {
        emulator.members[r.SharedGroupId] = (emulator.members[r.SharedGroupId] || []).filter(function(id) { return r.PlayFabIds.indexOf(id) < 0; });
        return {};
    },
    UpdateSharedGroupData: function(r) {
        var g = emulator.groups[r.SharedGroupId] = emulator.groups[r.SharedGroupId] || {};
        Object.keys(r.Data || {}).forEach(function(k) { g[k] = r.Data[k]; });
        (r.KeysToRemove || []).forEach(function(k) { delete g[k]; });
        return {};
    }
};
function holder(key) {
    var e = emulator.entities[key.Id];
    if (!e || key.Type !== 'title_player_account') throw {apiErrorInfo:{apiError:{error:'EntityNotFound'}}};
    return e;
}
var entity = {
    GetObjects: function(r) {
        var e = holder(r.Entity);
        emulator.reads += 1;
        var objects = {};
        Object.keys(e.objects).forEach(function(name) { objects[name] = {ObjectName:name, DataObject:emulator.clone(e.objects[name])}; });
        var response = {Objects:objects, ProfileVersion:e.version};
        if (emulator.interleave) emulator.interleave();
        return response;
    },
    SetObjects: function(r) {
        var e = holder(r.Entity);
        if (emulator.versioned && r.ExpectedProfileVersion !== e.version) {
            emulator.conflicts += 1;
            throw {apiErrorInfo:{apiError:{error:'EntityProfileVersionMismatch', errorCode:1405}}};
        }
        r.Objects.forEach(function(o) { e.objects[o.ObjectName] = emulator.clone(o.DataObject); });
        e.version += 1;
        return {ProfileVersion:e.version};
    },
    GetFiles: function(r) {
        var e = holder(r.Entity), metadata = {};
        Object.keys(e.files).forEach(function(name) {
            if (e.files[name].finalized) metadata[name] = {FileName:name, DownloadUrl:'ludissey-emulator://blob/' + r.Entity.Id + '/' + name, Size:e.files[name].size};
        });
        return {Metadata:metadata, ProfileVersion:e.version};
    },
    InitiateFileUploads: function(r) {
        var e = holder(r.Entity);
        return {UploadDetails:r.FileNames.map(function(name) {
            e.files[name] = e.files[name] || {finalized:false, size:0};
            return {FileName:name, UploadUrl:'ludissey-emulator://blob/' + r.Entity.Id + '/' + name};
        }), ProfileVersion:e.version};
    },
    FinalizeFileUploads: function(r) {
        var e = holder(r.Entity);
        r.FileNames.forEach(function(name) {
            var blob = emulator.blobs[r.Entity.Id + '/' + name];
            if (!e.files[name] || blob === undefined) throw {apiErrorInfo:{apiError:{error:'FileNotFound'}}};
            e.files[name].finalized = true;
            e.files[name].size = blob.length;
        });
        e.version += 1;
        return {ProfileVersion:e.version};
    }
};
var clientData = {
    GetSharedGroupData: function(player, r) {
        if ((emulator.members[r.SharedGroupId] || []).indexOf(player) < 0) throw {apiErrorInfo:{apiError:{error:'NotAuthorized'}}};
        return server.GetSharedGroupData(r);
    },
    UpdateSharedGroupData: function(player, r) {
        if ((emulator.members[r.SharedGroupId] || []).indexOf(player) < 0) throw {apiErrorInfo:{apiError:{error:'NotAuthorized'}}};
        return server.UpdateSharedGroupData({SharedGroupId:r.SharedGroupId, Data:r.Data, KeysToRemove:r.KeysToRemove});
    },
    GetUserData: function(player, r) {
        var p = emulator.player(player), data = {};
        (r.Keys || Object.keys(p.user)).forEach(function(k) { if (p.user[k] !== undefined) data[k] = {Value:p.user[k]}; });
        return {Data:data};
    },
    UpdateUserData: function(player, r) {
        var p = emulator.player(player);
        Object.keys(r.Data || {}).forEach(function(k) { p.user[k] = r.Data[k]; });
        (r.KeysToRemove || []).forEach(function(k) { delete p.user[k]; });
        return {};
    }
};
function emulatorInvoke(player, name, raw) {
    var previous = currentPlayerId;
    currentPlayerId = player;
    try {
        var handler = handlers[name];
        if (!handler) return JSON.stringify({error:'Funzione sconosciuta: ' + name});
        return JSON.stringify({result:handler(JSON.parse(raw))});
    } catch (error) {
        return JSON.stringify({error:typeof error === 'string' ? error : error instanceof Error ? 'Eccezione: ' + error.message : JSON.stringify(error)});
    } finally {
        currentPlayerId = previous;
    }
}
function emulatorClient(player, name, raw) {
    try {
        if (!clientData[name]) return JSON.stringify({error:'Metodo client non emulato: ' + name});
        return JSON.stringify({result:clientData[name](player, JSON.parse(raw))});
    } catch (error) {
        return JSON.stringify({error:JSON.stringify(error)});
    }
}
function emulatorProfile(player, username) {
    emulator.register(player, username);
    emulator.players[player].readOnly.LudisseyProfile = JSON.stringify({username:username, gender:'male', createdAt:new Date(emulator.time).toISOString()});
    return player;
}
