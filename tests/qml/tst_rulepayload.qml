// SPDX-License-Identifier: GPL-3.0-or-later

import QtTest

import "../../src/plasmoid/qml/rulepayload.js" as RulePayload

// Golden pins for the payload assembly module: every string here must remain
// byte-identical because the daemon's parser accepts exact key sets only.
TestCase {
    id: testCase

    name: "RokuDeckRulePayload"

    function blankWith(overrides) {
        const state = RulePayload.blankRule();
        for (const key in overrides) {
            if (key === "trigger") {
                for (const triggerKey in overrides.trigger) {
                    state.trigger[triggerKey] = overrides.trigger[triggerKey];
                }
            } else {
                state[key] = overrides[key];
            }
        }
        return state;
    }

    function leaf(device, field, op, value) {
        return { kind: "leaf", children: [], child: null,
                 device: device, field: field, op: op, value: value };
    }

    function group(kind, children) {
        return { kind: kind, children: children, child: null, device: "*",
                 field: "power", op: "is", value: "on" };
    }

    function notGroup(child) {
        return { kind: "not", children: [], child: child, device: "*",
                 field: "power", op: "is", value: "on" };
    }

    function test_manualKeypressRuleIsBuiltByteForByte() {
        const payload = RulePayload.buildPayload(blankWith({
            name: "Movie time",
            mode: "single",
            devices: ["X001"],
            trigger: { type: "manual" },
            actions: [{ type: "keypress", key: "Home" }]
        }), null);
        compare(payload,
                '{"name":"Movie time","mode":"single","devices":["X001"],'
                + '"trigger":{"type":"manual"},'
                + '"actions":[{"type":"keypress","key":"Home"}]}');
    }

    function test_weekdayScheduleWithVolumeAndDelayIsBuilt() {
        const payload = RulePayload.buildPayload(blankWith({
            name: "Weekday wind-down",
            devices: [],
            trigger: { type: "time", kind: "weekdays", time: "23:00" },
            actions: [{ type: "setVolume", volume: 15 }, { type: "delay", seconds: 30 }]
        }), null);
        compare(payload,
                '{"name":"Weekday wind-down","mode":"queued","devices":[],'
                + '"trigger":{"type":"time","kind":"weekdays","time":"23:00"},'
                + '"actions":[{"type":"setVolume","volume":15},'
                + '{"type":"delay","seconds":30}]}');
    }

    function test_onceScheduleWithLaunchAppCarriesTheDate() {
        const payload = RulePayload.buildPayload(blankWith({
            name: "One-time movie night",
            mode: "restart",
            devices: ["X002"],
            trigger: { type: "time", kind: "once", time: "19:30", date: "2026-09-01" },
            actions: [{ type: "launchApp", appId: "tv.netflix" }]
        }), null);
        compare(payload,
                '{"name":"One-time movie night","mode":"restart","devices":["X002"],'
                + '"trigger":{"type":"time","kind":"once","time":"19:30",'
                + '"date":"2026-09-01"},'
                + '"actions":[{"type":"launchApp","appId":"tv.netflix"}]}');
    }

    function test_intervalTriggerWithMuteActionIsBuilt() {
        const payload = RulePayload.buildPayload(blankWith({
            name: "Keep an ear on it",
            devices: ["X003"],
            trigger: { type: "interval", minutes: 30 },
            actions: [{ type: "mute", muted: true }]
        }), null);
        compare(payload,
                '{"name":"Keep an ear on it","mode":"queued","devices":["X003"],'
                + '"trigger":{"type":"interval","minutes":30},'
                + '"actions":[{"type":"mute","muted":true}]}');
    }

    function test_stateChangeTriggerWithBetweenValuesIsBuilt() {
        const state = blankWith({
            name: "Volume band watch",
            devices: [],
            trigger: { type: "stateChange", field: "volume", op: "between",
                       value: [20, 80] },
            actions: [{ type: "mute", muted: false }]
        });
        state.conditions = group("any", [
            leaf("*", "volume", "between", [10, 20]),
            notGroup(leaf("X001", "muted", "is", true))
        ]);
        const payload = RulePayload.buildPayload(state, null);
        compare(payload,
                '{"name":"Volume band watch","mode":"queued","devices":[],'
                + '"trigger":{"type":"stateChange","field":"volume","op":"between",'
                + '"value":[20,80]},'
                + '"conditions":{"any":[{"leaf":{"device":"*","field":"volume",'
                + '"op":"between","value":[10,20]}},'
                + '{"not":{"leaf":{"device":"X001","field":"muted","op":"is",'
                + '"value":true}}}]},"actions":[{"type":"mute","muted":false}]}');
    }

    function test_nestedAllAnyNotTreeSurvivesAssembly() {
        const state = blankWith({
            name: "Deep gate",
            devices: [],
            trigger: { type: "manual" },
            actions: [{ type: "keypress", key: "Select" }]
        });
        state.conditions = group("all", [
            group("any", [
                leaf("*", "power", "is", "on"),
                leaf("*", "health", "is", "online")
            ]),
            notGroup(group("any", [
                leaf("*", "screensaver", "is", "active"),
                leaf("X002", "app", "isNot", "netflix")
            ]))
        ]);
        const payload = RulePayload.buildPayload(state, null);
        compare(payload,
                '{"name":"Deep gate","mode":"queued","devices":[],'
                + '"trigger":{"type":"manual"},'
                + '"conditions":{"all":[{"any":[{"leaf":{"device":"*",'
                + '"field":"power","op":"is","value":"on"}},'
                + '{"leaf":{"device":"*","field":"health","op":"is",'
                + '"value":"online"}}]},'
                + '{"not":{"any":[{"leaf":{"device":"*","field":"screensaver",'
                + '"op":"is","value":"active"}},'
                + '{"leaf":{"device":"X002","field":"app","op":"isNot",'
                + '"value":"netflix"}}]}}]}'
                + ',"actions":[{"type":"keypress","key":"Select"}]}');
    }

    function test_ifThenElseWithBothBranchesIsBuilt() {
        const payload = RulePayload.buildPayload(blankWith({
            name: "Screensaver guard",
            devices: [],
            trigger: { type: "manual" },
            actions: [{
                type: "ifThenElse",
                cond: leaf("*", "screensaver", "is", "active"),
                thenActions: [{ type: "keypress", key: "Back" }],
                elseActions: [{ type: "delay", seconds: 5 }, { type: "keypress", key: "Select" }]
            }]
        }), null);
        compare(payload,
                '{"name":"Screensaver guard","mode":"queued","devices":[],'
                + '"trigger":{"type":"manual"},'
                + '"actions":[{"type":"ifThenElse",'
                + '"cond":{"leaf":{"device":"*","field":"screensaver","op":"is",'
                + '"value":"active"}},'
                + '"then":[{"type":"keypress","key":"Back"}],'
                + '"else":[{"type":"delay","seconds":5},'
                + '{"type":"keypress","key":"Select"}]}]}');
    }

    function test_anElselessIfThenElseOmitsTheElseKey() {
        const payload = RulePayload.buildPayload(blankWith({
            name: "Quiet branch",
            devices: [],
            trigger: { type: "manual" },
            actions: [{
                type: "ifThenElse",
                cond: leaf("*", "power", "is", "standby"),
                thenActions: [{ type: "launchApp", appId: "dev.rokudeck.test" }],
                elseActions: []
            }]
        }), null);
        verify(payload.indexOf('"else"') < 0,
               "an empty else branch must not serialize an else key");
    }

    readonly property string daemonDocument: '{"schemaVersion":1,"id":"rRoundTrip1",'
        + '"name":"Nightly home","enabled":true,"mode":"queued",'
        + '"devices":["SERIAL-A"],"trigger":{"type":"time","kind":"daily",'
        + '"time":"23:00"},"conditions":{"all":[{"leaf":{"device":"*",'
        + '"field":"power","op":"is","value":"on"}}]},'
        + '"actions":[{"type":"ifThenElse","cond":{"leaf":{"device":"*",'
        + '"field":"muted","op":"is","value":true}},'
        + '"then":[{"type":"setVolume","volume":15}],'
        + '"else":[{"type":"delay","seconds":5}]}],'
        + '"lastRun":{"lastFiredAt":"2026-08-24T03:00:00Z","outcome":"ok"}}'

    function test_editingRoundTripsWithoutDisturbingTheDocument() {
        const document = JSON.parse(daemonDocument);
        const state = RulePayload.toFormState(document);
        const rebuilt = RulePayload.buildPayload(state, document);
        compare(rebuilt, daemonDocument);
    }

    function test_aRenamedEditSplicesOnlyTheEditedFields() {
        const document = JSON.parse(daemonDocument);
        const state = RulePayload.toFormState(document);
        state.name = "Renamed at night";
        state.actions.push({ type: "runRule", id: "rOtherRule99" });
        const updated = JSON.parse(RulePayload.buildPayload(state, document));
        compare(updated.id, "rRoundTrip1");
        compare(updated.enabled, true);
        compare(updated.name, "Renamed at night");
        compare(updated.lastRun.outcome, "ok",
                "the loaded last-run record must survive an edit verbatim");
        compare(updated.actions.length, 2);
        compare(JSON.stringify(document.lastRun),
                '{"lastFiredAt":"2026-08-24T03:00:00Z","outcome":"ok"}');
    }

    function test_addingFromAnEditedRuleDropsDaemonOwnedFields() {
        const document = JSON.parse(daemonDocument);
        const payload = RulePayload.buildPayload(RulePayload.toFormState(document), null);
        const parsed = JSON.parse(payload);
        verify(parsed.id === undefined, "AddRule input must not carry id");
        verify(parsed.enabled === undefined, "AddRule input must not carry enabled");
        verify(parsed.schemaVersion === undefined,
               "the daemon injects schemaVersion itself");
        verify(parsed.lastRun === undefined, "AddRule input must not carry a last-run record");
        compare(parsed.name, "Nightly home");
        compare(parsed.conditions.all.length, 1);
    }

    function test_unknownFutureContentRidesAlongAndIsFlagged() {
        const document = JSON.parse(
            '{"schemaVersion":1,"id":"rFuture0001","name":"Telepathy","enabled":true,'
            + '"mode":"queued","devices":[],"trigger":{"type":"telepathy","focus":3},'
            + '"actions":[{"type":"teleport","target":"sofa"}]}');
        const state = RulePayload.toFormState(document);
        const rebuilt = JSON.parse(RulePayload.buildPayload(state, document));
        compare(rebuilt.trigger.type, "telepathy");
        compare(rebuilt.actions[0].type, "teleport");

        const problems = RulePayload.validate(state);
        let flaggedUnsupported = false;
        for (let i = 0; i < problems.length; ++i) {
            if (problems[i].code === "unsupportedContent") {
                flaggedUnsupported = true;
            }
        }
        verify(flaggedUnsupported, "unknown content must block a quiet save");
    }

    function test_blankFormStateStartsSane() {
        const state = RulePayload.blankRule();
        compare(state.mode, "queued");
        compare(state.devices, []);
        compare(state.trigger.type, "time");
        compare(state.conditions, null);
        compare(state.actions, []);
        // A fresh blank rule is incomplete but structurally sound: only the
        // missing name and actions are reported.
        const problems = RulePayload.validate(state);
        const codes = problems.map(problem => problem.code);
        verify(codes.indexOf("nameEmpty") >= 0);
        verify(codes.indexOf("actionsEmpty") >= 0);
        compare(codes.length, 2);
    }

    function test_validateRejectsBrokenForms() {
        const deepNotChain = (() => {
            let node = leaf("*", "power", "is", "on");
            for (let depth = 0; depth < 13; ++depth) {
                node = notGroup(node);
            }
            return node;
        })();
        const cases = [
            {
                tag: "empty-name",
                state: blankWith({ name: "", trigger: { type: "manual" } }),
                codes: ["actionsEmpty", "nameEmpty"]
            },
            {
                tag: "overlong-name",
                state: blankWith({
                    name: "x".repeat(81),
                    trigger: { type: "manual" },
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["nameTooLong"]
            },
            {
                tag: "malformed-time",
                state: blankWith({
                    name: "n", trigger: { type: "time", kind: "daily", time: "7:5" },
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["timeMalformed"]
            },
            {
                tag: "once-without-date",
                state: blankWith({
                    name: "n",
                    trigger: { type: "time", kind: "once", time: "07:05", date: "" },
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["dateMissing"]
            },
            {
                tag: "once-with-garbage-date",
                state: blankWith({
                    name: "n",
                    trigger: { type: "time", kind: "once", time: "07:05",
                               date: "2026-13-45" },
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["dateMissing"]
            },
            {
                tag: "interval-below-five",
                state: blankWith({
                    name: "n", trigger: { type: "interval", minutes: 4 },
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["intervalRange"]
            },
            {
                tag: "interval-above-a-day",
                state: blankWith({
                    name: "n", trigger: { type: "interval", minutes: 1441 },
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["intervalRange"]
            },
            {
                tag: "statechange-between-needs-two-numbers",
                state: blankWith({
                    name: "n",
                    trigger: { type: "stateChange", field: "volume", op: "between",
                               value: [20] },
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["betweenNeedsTwoNumbers"]
            },
            {
                tag: "statechange-volume-out-of-range",
                state: blankWith({
                    name: "n",
                    trigger: { type: "stateChange", field: "volume", op: "gt",
                               value: 140 },
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["volumeRange"]
            },
            {
                tag: "statechange-bad-power-value",
                state: blankWith({
                    name: "n",
                    trigger: { type: "stateChange", field: "power", op: "is",
                               value: "asleep" },
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["valueRequired"]
            },
            {
                tag: "condition-between-needs-two-numbers",
                state: blankWith({
                    name: "n", trigger: { type: "manual" },
                    conditions: leaf("*", "volume", "between", "oops"),
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["betweenNeedsTwoNumbers"]
            },
            {
                tag: "empty-condition-group",
                state: blankWith({
                    name: "n", trigger: { type: "manual" },
                    conditions: group("all", []),
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["conditionIncomplete"]
            },
            {
                tag: "not-without-child",
                state: blankWith({
                    name: "n", trigger: { type: "manual" },
                    conditions: notGroup(null),
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["conditionIncomplete"]
            },
            {
                tag: "unknown-keypress-key",
                state: blankWith({
                    name: "n", trigger: { type: "manual" },
                    actions: [{ type: "keypress", key: "Rewind" }]
                }),
                codes: ["keyInvalid"]
            },
            {
                tag: "bad-app-id",
                state: blankWith({
                    name: "n", trigger: { type: "manual" },
                    actions: [{ type: "launchApp", appId: "-starts-with-dash" }]
                }),
                codes: ["appIdInvalid"]
            },
            {
                tag: "delay-too-long",
                state: blankWith({
                    name: "n", trigger: { type: "manual" },
                    actions: [{ type: "delay", seconds: 601 }]
                }),
                codes: ["delayRange"]
            },
            {
                tag: "run-rule-target-empty",
                state: blankWith({
                    name: "n", trigger: { type: "manual" },
                    actions: [{ type: "runRule", id: "" }]
                }),
                codes: ["ruleTargetInvalid"]
            },
            {
                tag: "if-branch-empty",
                state: blankWith({
                    name: "n", trigger: { type: "manual" },
                    actions: [{
                        type: "ifThenElse",
                        cond: leaf("*", "power", "is", "on"),
                        thenActions: [],
                        elseActions: []
                    }]
                }),
                codes: ["branchEmpty"]
            },
            {
                tag: "no-actions-at-all",
                state: blankWith({ name: "n", trigger: { type: "manual" } }),
                codes: ["actionsEmpty"]
            },
            {
                tag: "deeply-nested-conditions",
                state: blankWith({
                    name: "n", trigger: { type: "manual" },
                    conditions: deepNotChain,
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["tooDeep"]
            },
            {
                tag: "valid-rule-reports-nothing",
                state: blankWith({
                    name: "Fine",
                    trigger: { type: "interval", minutes: 15 },
                    actions: [{ type: "setVolume", volume: 42 }],
                    conditions: group("any", [leaf("*", "muted", "is", false)])
                }),
                codes: []
            }
        ];
        for (let i = 0; i < cases.length; ++i) {
            const entry = cases[i];
            const codes = RulePayload.validate(entry.state)
                .map(problem => problem.code).sort();
            compare(JSON.stringify(codes), JSON.stringify(entry.codes),
                    "problem codes for " + entry.tag);
        }
    }

    function test_typedValuesNormalizeAcrossTheRoundTrip() {
        const document = JSON.parse(
            '{"schemaVersion":1,"id":"rTypedValue1","name":"Typed","enabled":true,'
            + '"mode":"queued","devices":[],'
            + '"trigger":{"type":"stateChange","field":"muted","op":"is","value":true},'
            + '"actions":[{"type":"setVolume","volume":33}]}');
        const state = RulePayload.toFormState(document);
        compare(state.trigger.value, true);
        const rebuilt = JSON.parse(RulePayload.buildPayload(state, document));
        compare(rebuilt.trigger.value, true, "booleans stay booleans");
        compare(typeof rebuilt.actions[0].volume, "number", "volumes stay numeric");
    }
}
