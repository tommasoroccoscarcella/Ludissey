function avatarLimit(category) {
    if (category === 'hat') return RULES.avatar.limits.hats;
    return RULES.avatar.accessories.indexOf(category) >= 0 ? RULES.avatar.limits.accessories : 1;
}
function avatarValidate(raw, name, body, inventory, catalog) {
    var a = raw && typeof raw === 'object' ? raw : {}, rules = RULES.avatar;
    var result = {displayName:name, bodyPreset:rules.bodyPresets.indexOf(body) >= 0 ? body : rules.standard.bodyPreset, bodyColors:{}, equipped:[]};
    var shown = typeof a.displayName === 'string' ? a.displayName.trim() : '';
    if (shown.length >= 3 && shown.length <= rules.nameCharacters) result.displayName = shown;
    if (rules.bodyPresets.indexOf(a.bodyPreset) >= 0) result.bodyPreset = a.bodyPreset;
    var colors = a.bodyColors && typeof a.bodyColors === 'object' ? a.bodyColors : {head:a.skinTone, leftArm:a.skinTone, rightArm:a.skinTone, torso:a.shirtColor, leftLeg:a.pantsColor, rightLeg:a.pantsColor};
    rules.bodyParts.forEach(function(part) { result.bodyColors[part] = rules.palette.indexOf(colors[part]) >= 0 ? colors[part] : rules.standard.bodyColors[part]; });
    var owned = {}, known = {};
    inventory.forEach(function(entry) { owned[entry.itemID] = true; });
    catalog.forEach(function(item) { known[item.id] = item; });
    (Array.isArray(a.equipped) ? a.equipped : []).forEach(function(entry) {
        if (!entry || !owned[entry.itemID] || !known[entry.itemID] || known[entry.itemID].category !== entry.category) return;
        if (result.equipped.some(function(e) { return e.itemID === entry.itemID; })) return;
        var same = result.equipped.filter(function(e) { return e.category === entry.category; });
        if (avatarLimit(entry.category) === 1) result.equipped = result.equipped.filter(function(e) { return e.category !== entry.category; });
        else if (same.length >= avatarLimit(entry.category)) return;
        var accessories = result.equipped.filter(function(e) { return rules.accessories.indexOf(e.category) >= 0; }).length;
        if (rules.accessories.indexOf(entry.category) >= 0 && accessories >= rules.limits.accessories) return;
        result.equipped.push({itemID:entry.itemID, category:entry.category});
    });
    return result;
}
