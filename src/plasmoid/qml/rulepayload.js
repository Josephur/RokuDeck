// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// Pure assembly of automation-rule payloads in the exact shape the daemon's
// strict parser (src/core/rule_model.cpp) accepts. The daemon remains the sole
// validation authority; validate() below is a convenience gate so obviously
// broken forms never make a round trip.

var modes = ["single", "restart", "queued"];
var timeKinds = ["daily", "weekdays", "once"];
var fields = ["power", "screensaver", "app", "muted", "volume", "health"];
var ops = ["is", "isNot", "gt", "lt", "between"];
var powerValues = ["on", "standby", "unknown"];
var screensaverValues = ["active", "inactive", "unknown"];
var healthValues = ["online", "standby", "degraded", "offline"];
var keypressKeys = ["Home", "Back", "Up", "Down", "Left", "Right", "Select",
                    "Play", "Pause", "Rev", "Fwd", "InstantReplay",
                    "FindRemote", "Power", "PowerOn"];
var actionTypes = ["keypress", "launchApp", "setVolume", "mute", "delay",
                   "runRule", "ifThenElse"];

function appIdIsValid(appId) {
    return /^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(String(appId));
}

function ruleIdIsValid(ruleId) {
    return /^[A-Za-z0-9_-]{1,64}$/.test(String(ruleId));
}

function timeIsValid(time) {
    return /^([01][0-9]|2[0-3]):[0-5][0-9]$/.test(String(time));
}

function dateIsValid(date) {
    if (!/^[0-9]{4}-[0-9]{2}-[0-9]{2}$/.test(String(date))) {
        return false;
    }
    const parsed = new Date(String(date) + "T00:00:00");
    return !isNaN(parsed.getTime());
}

// A fresh form state for the add flow; every picker starts coherent.
function blankRule() {
    return {
        name: "",
        mode: "queued",
        devices: [],
        trigger: {
            type: "time",
            kind: "daily",
            time: "08:00",
            date: "",
            minutes: 30,
            field: "power",
            op: "is",
            value: "on"
        },
        conditions: null,
        actions: []
    };
}

function canonicalValue(field, op, value) {
    if (op === "between") {
        const low = typeof value === "object" && value !== null && value.length === 2
            ? Number(value[0]) : NaN;
        const high = typeof value === "object" && value !== null && value.length === 2
            ? Number(value[1]) : NaN;
        return [isNaN(low) ? 0 : low, isNaN(high) ? 100 : high];
    }
    if (field === "muted") {
        return value === true;
    }
    if (field === "volume") {
        const number = Number(value);
        return isNaN(number) ? 0 : number;
    }
    return String(value === undefined || value === null ? "" : value);
}

function triggerToForm(trigger) {
    const state = blankRule().trigger;
    if (!trigger || typeof trigger !== "object") {
        return state;
    }
    switch (String(trigger.type)) {
    case "manual":
        return { type: "manual", kind: state.kind, time: state.time, date: "",
                 minutes: state.minutes, field: state.field, op: state.op,
                 value: state.value };
    case "time": {
        const kind = timeKinds.indexOf(String(trigger.kind)) >= 0
            ? String(trigger.kind) : "daily";
        return { type: "time", kind: kind,
                 time: timeIsValid(trigger.time) ? String(trigger.time) : state.time,
                 date: kind === "once" ? String(trigger.date ?? "") : "",
                 minutes: state.minutes, field: state.field, op: state.op,
                 value: state.value };
    }
    case "interval": {
        const minutes = Number(trigger.minutes);
        return { type: "interval", kind: state.kind, time: state.time, date: "",
                 minutes: isNaN(minutes) ? state.minutes : minutes,
                 field: state.field, op: state.op, value: state.value };
    }
    case "stateChange":
        return { type: "stateChange", kind: state.kind, time: state.time, date: "",
                 minutes: state.minutes,
                 field: fields.indexOf(String(trigger.field)) >= 0
                     ? String(trigger.field) : state.field,
                 op: ops.indexOf(String(trigger.op)) >= 0 ? String(trigger.op) : state.op,
                 value: canonicalValue(String(trigger.field), String(trigger.op),
                                       trigger.value) };
    default:
        // Unknown future trigger: carried verbatim so editing anything else in
        // the rule never silently drops it; validate() flags the rule.
        return Object.assign({}, trigger);
    }
}

function nodeToForm(json) {
    if (!json || typeof json !== "object") {
        return null;
    }
    const base = { kind: "leaf", children: [], child: null, device: "*",
                   field: "power", op: "is", value: "on" };
    if (json.leaf && typeof json.leaf === "object") {
        const leaf = json.leaf;
        base.device = String(leaf.device === undefined || leaf.device === null
                             || leaf.device === "" ? "*" : leaf.device);
        base.field = fields.indexOf(String(leaf.field)) >= 0 ? String(leaf.field) : base.field;
        base.op = ops.indexOf(String(leaf.op)) >= 0 ? String(leaf.op) : base.op;
        base.value = canonicalValue(base.field, base.op, leaf.value);
        return base;
    }
    if (json.not) {
        base.kind = "not";
        base.child = nodeToForm(json.not);
        return base;
    }
    const groupKey = json.all ? "all" : (json.any ? "any" : "");
    if (groupKey.length === 0) {
        return null;
    }
    base.kind = groupKey;
    const entries = Array.isArray(json[groupKey]) ? json[groupKey] : [];
    for (let i = 0; i < entries.length; ++i) {
        const child = nodeToForm(entries[i]);
        if (child) {
            base.children.push(child);
        }
    }
    return base;
}

function blankLeaf() {
    return { kind: "leaf", children: [], child: null, device: "*", field: "power",
             op: "is", value: "on" };
}

function actionToForm(action) {
    if (!action || typeof action !== "object") {
        return { type: "keypress", key: "Home", appId: "", volume: 50,
                 muted: false, seconds: 10, id: "", cond: null,
                 thenActions: [], elseActions: [] };
    }
    const state = { type: String(action.type), key: "Home", appId: "", volume: 50,
                    muted: false, seconds: 10, id: "", cond: null,
                    thenActions: [], elseActions: [], passthrough: null };
    switch (state.type) {
    case "keypress":
        state.key = String(action.key === undefined ? "Home" : action.key);
        break;
    case "launchApp":
        state.appId = String(action.appId === undefined ? "" : action.appId);
        break;
    case "setVolume": {
        const volume = Number(action.volume);
        state.volume = isNaN(volume) ? 50 : volume;
        break;
    }
    case "mute":
        state.muted = action.muted === true;
        break;
    case "delay": {
        const seconds = Number(action.seconds);
        state.seconds = isNaN(seconds) ? 10 : seconds;
        break;
    }
    case "runRule":
        state.id = String(action.id === undefined ? "" : action.id);
        break;
    case "ifThenElse":
        state.cond = nodeToForm(action.cond);
        state.thenActions = (Array.isArray(action.then) ? action.then : [])
            .map(actionToForm);
        state.elseActions = (Array.isArray(action.else) ? action.else : [])
            .map(actionToForm);
        break;
    default:
        state.passthrough = Object.assign({}, action);
        break;
    }
    return state;
}

function toFormState(rule) {
    const source = rule && typeof rule === "object" ? rule : {};
    const state = blankRule();
    state.name = String(source.name === undefined ? "" : source.name);
    state.mode = modes.indexOf(String(source.mode)) >= 0 ? String(source.mode) : state.mode;
    state.devices = Array.isArray(source.devices) ? source.devices.map(String) : [];
    // Unknown trigger kinds ride along verbatim; validate() flags the rule so
    // it is never silently rewritten.
    state.trigger = triggerToForm(source.trigger);
    state.conditions = nodeToForm(source.conditions);
    state.actions = (Array.isArray(source.actions) ? source.actions : []).map(actionToForm);
    return state;
}

function conditionValueJson(node) {
    if (node.op === "between") {
        const pair = Array.isArray(node.value) && node.value.length === 2
            ? node.value : [0, 100];
        return [Number(pair[0]), Number(pair[1])];
    }
    if (node.field === "muted") {
        return node.value === true;
    }
    if (node.field === "volume") {
        return Number(node.value);
    }
    return String(node.value === undefined || node.value === null ? "" : node.value);
}

function conditionNodeJson(node) {
    if (!node) {
        return null;
    }
    if (node.kind === "not") {
        return { not: conditionNodeJson(node.child) };
    }
    if (node.kind === "all" || node.kind === "any") {
        const children = [];
        const entries = Array.isArray(node.children) ? node.children : [];
        for (let i = 0; i < entries.length; ++i) {
            children.push(conditionNodeJson(entries[i]));
        }
        const group = {};
        group[node.kind] = children;
        return group;
    }
    return { leaf: { device: String(node.device || "*"), field: String(node.field),
                     op: String(node.op), value: conditionValueJson(node) } };
}

function triggerJson(trigger) {
    switch (String(trigger.type)) {
    case "manual":
        return { type: "manual" };
    case "time": {
        const built = { type: "time", kind: String(trigger.kind),
                        time: String(trigger.time) };
        if (String(trigger.kind) === "once") {
            built.date = String(trigger.date);
        }
        return built;
    }
    case "interval":
        return { type: "interval", minutes: Number(trigger.minutes) };
    case "stateChange":
        return { type: "stateChange", field: String(trigger.field),
                 op: String(trigger.op),
                 value: conditionValueJson({ field: String(trigger.field),
                                             op: String(trigger.op),
                                             value: trigger.value }) };
    default:
        return Object.assign({}, trigger);
    }
}

function actionJson(action) {
    if (action && action.passthrough) {
        return Object.assign({}, action.passthrough);
    }
    switch (String(action.type)) {
    case "keypress":
        return { type: "keypress", key: String(action.key) };
    case "launchApp":
        return { type: "launchApp", appId: String(action.appId) };
    case "setVolume":
        return { type: "setVolume", volume: Number(action.volume) };
    case "mute":
        return { type: "mute", muted: action.muted === true };
    case "delay":
        return { type: "delay", seconds: Number(action.seconds) };
    case "runRule":
        return { type: "runRule", id: String(action.id) };
    case "ifThenElse": {
        const built = { type: "ifThenElse", cond: conditionNodeJson(action.cond) };
        const thenSteps = [];
        const thenEntries = Array.isArray(action.thenActions) ? action.thenActions : [];
        for (let i = 0; i < thenEntries.length; ++i) {
            thenSteps.push(actionJson(thenEntries[i]));
        }
        built.then = thenSteps;
        if (Array.isArray(action.elseActions) && action.elseActions.length > 0) {
            const elseSteps = [];
            for (let i = 0; i < action.elseActions.length; ++i) {
                elseSteps.push(actionJson(action.elseActions[i]));
            }
            built.else = elseSteps;
        }
        return built;
    }
    default:
        return Object.assign({}, action);
    }
}

// JSON string for AddRule when existingRule is absent, otherwise the complete
// UpdateRule document with id/enabled/lastRun preserved verbatim from the
// loaded rule and only the edited blocks spliced over.
function buildPayload(formState, existingRule) {
    const payload = existingRule && typeof existingRule === "object"
        ? Object.assign({}, existingRule) : {};
    payload.name = String(formState.name);
    payload.mode = String(formState.mode);
    payload.devices = (Array.isArray(formState.devices) ? formState.devices : []).map(String);
    payload.trigger = triggerJson(formState.trigger);
    if (formState.conditions) {
        payload.conditions = conditionNodeJson(formState.conditions);
    } else {
        delete payload.conditions;
    }
    const actions = [];
    const entries = Array.isArray(formState.actions) ? formState.actions : [];
    for (let i = 0; i < entries.length; ++i) {
        actions.push(actionJson(entries[i]));
    }
    payload.actions = actions;
    return JSON.stringify(payload);
}

function validatePredicate(field, op, value) {
    const problems = [];
    if (fields.indexOf(String(field)) < 0 || ops.indexOf(String(op)) < 0) {
        problems.push({ code: "unsupportedContent" });
        return problems;
    }
    if (op === "between") {
        if (!Array.isArray(value) || value.length !== 2
            || isNaN(Number(value[0])) || isNaN(Number(value[1]))) {
            problems.push({ code: "betweenNeedsTwoNumbers" });
        }
        return problems;
    }
    if ((field === "power" && powerValues.indexOf(String(value)) < 0)
        || (field === "screensaver" && screensaverValues.indexOf(String(value)) < 0)
        || (field === "health" && healthValues.indexOf(String(value)) < 0)) {
        problems.push({ code: "valueRequired" });
        return problems;
    }
    if (field === "volume") {
        const volume = Number(value);
        if (isNaN(volume) || volume < 0 || volume > 100) {
            problems.push({ code: "volumeRange" });
        }
    }
    return problems;
}

function validateNodeDepth(node, depth) {
    if (!node) {
        return [{ code: "conditionIncomplete" }];
    }
    if (depth > 12) {
        return [{ code: "tooDeep" }];
    }
    if (node.kind === "leaf") {
        return validatePredicate(node.field, node.op, node.value);
    }
    if (node.kind === "not") {
        return validateNodeDepth(node.child, depth + 1);
    }
    const problems = [];
    const children = Array.isArray(node.children) ? node.children : [];
    if (children.length === 0) {
        problems.push({ code: "conditionIncomplete" });
    }
    for (let i = 0; i < children.length; ++i) {
        problems.push.apply(problems, validateNodeDepth(children[i], depth + 1));
    }
    return problems;
}

function validateAction(action) {
    if (!action) {
        return [{ code: "actionsEmpty" }];
    }
    if (action.passthrough) {
        return [{ code: "unsupportedContent" }];
    }
    switch (String(action.type)) {
    case "keypress":
        return keypressKeys.indexOf(String(action.key)) >= 0
            ? [] : [{ code: "keyInvalid" }];
    case "launchApp":
        return appIdIsValid(action.appId) ? [] : [{ code: "appIdInvalid" }];
    case "setVolume": {
        const volume = Number(action.volume);
        return !isNaN(volume) && volume >= 0 && volume <= 100
            ? [] : [{ code: "volumeRange" }];
    }
    case "mute":
        return [];
    case "delay": {
        const seconds = Number(action.seconds);
        return !isNaN(seconds) && seconds >= 1 && seconds <= 600
            ? [] : [{ code: "delayRange" }];
    }
    case "runRule":
        return ruleIdIsValid(action.id) ? [] : [{ code: "ruleTargetInvalid" }];
    case "ifThenElse": {
        const problems = validateNodeDepth(action.cond, 0);
        const thenEntries = Array.isArray(action.thenActions) ? action.thenActions : [];
        if (thenEntries.length === 0) {
            problems.push({ code: "branchEmpty" });
        }
        for (let i = 0; i < thenEntries.length; ++i) {
            problems.push.apply(problems, validateAction(thenEntries[i]));
        }
        const elseEntries = Array.isArray(action.elseActions) ? action.elseActions : [];
        for (let i = 0; i < elseEntries.length; ++i) {
            problems.push.apply(problems, validateAction(elseEntries[i]));
        }
        return problems;
    }
    default:
        return [{ code: "unsupportedContent" }];
    }
}

// Convenience gate only: the daemon parser stays authoritative.
function validate(formState) {
    const problems = [];
    const name = String(formState.name === undefined ? "" : formState.name);
    if (name.trim().length === 0) {
        problems.push({ code: "nameEmpty" });
    } else if (name.length > 80) {
        problems.push({ code: "nameTooLong" });
    }

    const trigger = formState.trigger ?? {};
    switch (String(trigger.type)) {
    case "time":
        if (!timeIsValid(trigger.time)) {
            problems.push({ code: "timeMalformed" });
        }
        if (String(trigger.kind) === "once" && !dateIsValid(trigger.date)) {
            problems.push({ code: "dateMissing" });
        }
        break;
    case "interval": {
        const minutes = Number(trigger.minutes);
        if (isNaN(minutes) || minutes < 5 || minutes > 1440) {
            problems.push({ code: "intervalRange" });
        }
        break;
    }
    case "stateChange":
        problems.push.apply(problems,
                            validatePredicate(trigger.field, trigger.op, trigger.value));
        break;
    case "manual":
        break;
    default:
        problems.push({ code: "unsupportedContent" });
        break;
    }

    // Extra conditions are optional; only a present tree must be complete.
    if (formState.conditions) {
        problems.push.apply(problems, validateNodeDepth(formState.conditions, 0));
    }

    const actions = Array.isArray(formState.actions) ? formState.actions : [];
    if (actions.length === 0) {
        problems.push({ code: "actionsEmpty" });
    }
    for (let i = 0; i < actions.length; ++i) {
        problems.push.apply(problems, validateAction(actions[i]));
    }
    return problems;
}
