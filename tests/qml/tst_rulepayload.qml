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

    function test_theEnabledSwitchIsAnEditedField() {
        const document = JSON.parse(daemonDocument);
        const state = RulePayload.toFormState(document);
        verify(state.enabled);
        state.enabled = false;
        const updated = JSON.parse(RulePayload.buildPayload(state, document));
        compare(updated.enabled, false, "a flipped switch must reach the document");
        compare(updated.id, "rRoundTrip1");
        compare(updated.lastRun.outcome, "ok");

        const added = JSON.parse(
            RulePayload.buildPayload(RulePayload.toFormState(document), null));
        verify(added.enabled === undefined,
               "the enabled choice belongs to the daemon on add");
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
            // One level deeper than the daemon's maximumDepth (16).
            for (let depth = 0; depth < 17; ++depth) {
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
                tag: "window-degenerate",
                state: blankWith({
                    name: "n",
                    trigger: { type: "manual", windowEnabled: true,
                               windowStart: "22:00", windowEnd: "22:00" },
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["windowDegenerate"]
            },
            {
                tag: "window-malformed",
                state: blankWith({
                    name: "n",
                    trigger: { type: "manual", windowEnabled: true,
                               windowStart: "7:5", windowEnd: "" },
                    actions: [{ type: "keypress", key: "Home" }]
                }),
                codes: ["windowMalformed"]
            },
            {
                tag: "script-name-invalid",
                state: blankWith({
                    name: "n", trigger: { type: "manual" },
                    actions: [{ type: "runScript", script: "Movie Night!" }]
                }),
                codes: ["scriptNameInvalid"]
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

    // A field or comparison change must never leave a value that means
    // nothing for the new pair, or the picker and the stored value disagree.
    function test_canonicalValueForFieldRepairsMeaninglessValues() {
        compare(RulePayload.canonicalValueForField("power", "is", 50), "on");
        compare(RulePayload.canonicalValueForField("muted", "is", "50"), false);
        compare(RulePayload.canonicalValueForField("playing", "is", "true"), false);
        compare(RulePayload.canonicalValueForField("volume", "is", "on"), 0);
        compare(RulePayload.canonicalValueForField("screensaver", "is", true), "inactive");
        compare(RulePayload.canonicalValueForField("volume", "between", 50), [0, 100]);
        compare(RulePayload.canonicalValueForField("volume", "between", [10, 20]), [10, 20]);
        // A value that still makes sense survives the switch untouched.
        compare(RulePayload.canonicalValueForField("power", "is", "standby"), "standby");
        compare(RulePayload.canonicalValueForField("health", "is", "degraded"), "degraded");
    }

    function test_playingFieldJoinsTheVocabulary() {
        verify(RulePayload.fields.indexOf("playing") >= 0,
               "the editor field picker must list playing");
        compare(RulePayload.canonicalValue("playing", "is", true), true);
        compare(RulePayload.canonicalValue("playing", "is", false), false);
        // Strings and numbers collapse to a plain boolean, mirroring muted.
        compare(RulePayload.canonicalValue("playing", "is", "true"), false);
        const leaf = RulePayload.nodeToForm(
            { leaf: { device: "LOCAL-PC", field: "playing", op: "is", value: true } });
        compare(leaf.value, true);
        compare(RulePayload.conditionValueJson(leaf), true);

        const problems = RulePayload.validatePredicate("playing", "is", true);
        compare(problems.length, 0);

        // media joins the enum-field family: canonical values, round-trip,
        // and validation reject nonsense just like power/health.
        verify(RulePayload.fields.indexOf("media") >= 0,
               "the editor field picker must list media");
        compare(RulePayload.canonicalValue("media", "is", "paused"), "paused");
        const mediaLeaf = RulePayload.nodeToForm(
            { leaf: { device: "*", field: "media", op: "is", value: "playing" } });
        compare(mediaLeaf.value, "playing");
        compare(RulePayload.conditionValueJson(mediaLeaf), "playing");
        const mediaProblems =
            RulePayload.validatePredicate("media", "is", "loud");
        verify(mediaProblems.length > 0, "nonsense media values must be flagged");
    }

    function test_playingStateChangeRoundTripsByteForByte() {
        const payload = RulePayload.buildPayload(blankWith({
            name: "Now playing",
            devices: ["LOCAL-PC"],
            trigger: { type: "stateChange", field: "playing", op: "is", value: true },
            actions: [{ type: "mute", muted: true }]
        }), null);
        compare(payload,
                '{"name":"Now playing","mode":"queued","devices":["LOCAL-PC"],'
                + '"trigger":{"type":"stateChange","field":"playing","op":"is","value":true},'
                + '"actions":[{"type":"mute","muted":true}]}');
    }

    function test_deviceScopedStateChangeTriggerIsBuilt() {
        const payload = RulePayload.buildPayload(blankWith({
            name: "This PC plays",
            devices: ["LOCAL-PC"],
            trigger: { type: "stateChange", device: "LOCAL-PC", field: "playing",
                       op: "is", value: true },
            actions: [{ type: "mute", muted: true }]
        }), null);
        compare(payload,
                '{"name":"This PC plays","mode":"queued","devices":["LOCAL-PC"],'
                + '"trigger":{"type":"stateChange","device":"LOCAL-PC",'
                + '"field":"playing","op":"is","value":true},'
                + '"actions":[{"type":"mute","muted":true}]}');

        // Any device stays implicit, exactly like today's rules.
        const anyPayload = RulePayload.buildPayload(blankWith({
            name: "Any tv", devices: [],
            trigger: { type: "stateChange", device: "*", field: "power",
                       op: "is", value: "on" },
            actions: [{ type: "keypress", key: "Home" }]
        }), null);
        verify(anyPayload.indexOf('"device"') < 0,
               "a wildcard scope must not serialize a device key");
    }

    function test_activeWindowRoundsTripThroughTheForm() {
        const state = blankWith({
            name: "Overnight guard",
            devices: ["LOCAL-PC"],
            trigger: { type: "time", kind: "daily", time: "23:00",
                       windowEnabled: true, windowStart: "22:00", windowEnd: "08:00" },
            actions: [{ type: "mute", muted: true }]
        });
        const payload = RulePayload.buildPayload(state, null);
        compare(payload,
                '{"name":"Overnight guard","mode":"queued","devices":["LOCAL-PC"],'
                + '"trigger":{"type":"time","kind":"daily","time":"23:00",'
                + '"activeWindow":{"start":"22:00","end":"08:00"}},'
                + '"actions":[{"type":"mute","muted":true}]}');

        // The stored document reads back into the same form shape...
        const form = RulePayload.toFormState(JSON.parse(payload));
        compare(form.trigger.windowEnabled, true);
        compare(form.trigger.windowStart, "22:00");
        compare(form.trigger.windowEnd, "08:00");
        // ...and rebuilds byte-for-byte.
        const rebuilt = JSON.parse(
            RulePayload.buildPayload(form, JSON.parse(payload)));
        compare(JSON.stringify(rebuilt), JSON.stringify(JSON.parse(payload)));

        // Anytime (the default) never emits a window block.
        const anytime = RulePayload.buildPayload(blankWith({
            name: "Anytime", devices: [], trigger: { type: "manual" },
            actions: [{ type: "keypress", key: "Home" }]
        }), null);
        verify(anytime.indexOf("activeWindow") < 0,
               "an absent window must not serialize");
    }

    function test_runScriptStepIsBuiltAndValidated() {
        const payload = RulePayload.buildPayload(blankWith({
            name: "Movie night",
            devices: ["LOCAL-PC"],
            trigger: { type: "manual" },
            actions: [{ type: "runScript", script: "movie-night" }]
        }), null);
        compare(payload,
                '{"name":"Movie night","mode":"queued","devices":["LOCAL-PC"],'
                + '"trigger":{"type":"manual"},'
                + '"actions":[{"type":"runScript","script":"movie-night"}]}');

        const form = RulePayload.toFormState(JSON.parse(payload));
        compare(form.actions[0].type, "runScript");
        compare(form.actions[0].script, "movie-night");
        compare(RulePayload.validate(form).length, 0);

        // Client-side gate mirrors the daemon's name regex.
        form.actions[0].script = "../evil";
        let flagged = false;
        for (const problem of RulePayload.validate(form)) {
            if (problem.code === "scriptNameInvalid") {
                flagged = true;
            }
        }
        verify(flagged, "a path-ish script name must be flagged");
        form.actions[0].script = "Movie-Night";
        compare(RulePayload.validate(form)
                    .filter(problem => problem.code === "scriptNameInvalid").length,
                1, "an uppercase script name must be flagged too");
    }

    // The client gate must accept exactly what the daemon parser accepts and
    // reject what it rejects, or the editor blocks valid rules (or lets an
    // invalid one make a pointless round trip).
    function test_clientValidationMatchesTheDaemon() {
        verify(RulePayload.dateIsValid("2024-02-29"), "a leap day exists");
        verify(!RulePayload.dateIsValid("2026-02-29"), "2026 is not a leap year");
        verify(!RulePayload.dateIsValid("2026-02-31"),
               "an impossible calendar day must be rejected, not rolled over");
        verify(!RulePayload.dateIsValid("2026-13-01"));

        // Volume keys are valid keypress actions server-side.
        verify(RulePayload.keypressKeys.indexOf("VolumeUp") >= 0);
        verify(RulePayload.keypressKeys.indexOf("VolumeMute") >= 0);

        const actions = [];
        for (let i = 0; i < 33; ++i) {
            actions.push({ type: "keypress", key: "Home" });
        }
        const problems = RulePayload.validate({
            name: "Many", mode: "queued", devices: [], conditions: null,
            trigger: { type: "manual" },
            actions: actions
        });
        verify(problems.some(problem => problem.code === "tooManyActions"),
               "33 top-level actions exceed the daemon's cap");
    }
}
