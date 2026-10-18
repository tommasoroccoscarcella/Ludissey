var accounts = {};
var recovery = [];
var sessions = {};
var calls = [];
function webReply(code, body) {
    return Promise.resolve({ok:code === 200, status:code, json:function() { return Promise.resolve(body); }});
}
function webFailure(error) { return webReply(400, {code:400, error:error, errorMessage:error}); }
function webEnroll(id, username) {
    emulatorProfile(id, username);
    accounts[username.toLowerCase()] = {id:id, username:username, password:null};
    return id;
}
function webInvoke(player, name, args) {
    var outcome = JSON.parse(emulatorInvoke(player, name, JSON.stringify(args || {})));
    if (outcome.error !== undefined) throw new Error(outcome.error);
    return outcome.result;
}
function webClient(method, request, ticket) {
    calls.push(method);
    if (method === 'RegisterPlayFabUser' || method === 'LoginWithPlayFab') {
        var key = String(request.Username).toLowerCase(), existing = accounts[key];
        if (method === 'RegisterPlayFabUser') {
            if (existing) return webFailure('UsernameNotAvailable');
            existing = accounts[key] = {id:'W' + (Object.keys(accounts).length + 1), username:request.Username, password:request.Password};
            emulator.register(existing.id, request.Username);
        } else if (!existing || existing.password === null || existing.password !== request.Password) return webFailure('InvalidUsernameOrPassword');
        emulator.serial += 1;
        var issued = 'ticket-' + existing.id + '-' + emulator.serial;
        sessions[issued] = existing.id;
        return webReply(200, {code:200, data:{SessionTicket:issued, PlayFabId:existing.id}});
    }
    if (method === 'SendAccountRecoveryEmail') {
        recovery.push(request.Email);
        return webReply(200, {code:200, data:{}});
    }
    var player = sessions[ticket];
    if (!player) return webFailure('InvalidSessionTicket');
    if (method === 'ExecuteCloudScript') {
        var outcome = JSON.parse(emulatorInvoke(player, request.FunctionName, JSON.stringify(request.FunctionParameter || {})));
        if (outcome.error !== undefined) return webReply(200, {code:200, data:{Error:{Message:'JavascriptException'}, Logs:[{Data:{Error:outcome.error}}]}});
        return webReply(200, {code:200, data:{FunctionResult:outcome.result}});
    }
    if (method === 'GetAccountInfo') {
        var found = request.Username ? accounts[String(request.Username).toLowerCase()] : {id:player};
        if (!found) return webFailure('AccountNotFound');
        return webReply(200, {code:200, data:{AccountInfo:{PlayFabId:found.id}}});
    }
    if (method === 'GetTitleData') return webReply(200, {code:200, data:server.GetTitleData(request)});
    if (method === 'GetUserData' && request.PlayFabId) return webReply(200, {code:200, data:server.GetUserData(request)});
    var reply = JSON.parse(emulatorClient(player, method, JSON.stringify(request)));
    if (reply.error !== undefined) return webFailure(reply.error);
    return webReply(200, {code:200, data:reply.result});
}
var emulatorFiles = entity.GetFiles;
entity.GetFiles = function(r) {
    var reply = emulatorFiles(r);
    Object.keys(reply.Metadata).forEach(function(name) {
        var blob = emulator.blobs[r.Entity.Id + '/' + name];
        if (typeof blob === 'string') reply.Metadata[name].DownloadUrl = 'data:image/' + (/\.png$/.test(name) ? 'png' : 'jpeg') + ';base64,' + blob;
    });
    return reply;
};
var nativeFetch = window.fetch;
window.fetch = function(url, options) {
    var prefix = 'https://' + RULES.account.titleId + '.playfabapi.com/Client/';
    if (String(url).indexOf(prefix) !== 0) return nativeFetch.apply(window, arguments);
    return webClient(String(url).slice(prefix.length), JSON.parse(options.body || '{}'), (options.headers || {})['X-Authorization']);
};
runtime.clock = function() { return Date.now(); };
