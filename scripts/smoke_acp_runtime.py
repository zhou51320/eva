#!/usr/bin/env python3
"""Small ACP runtime smoke helper for target machines.

The script intentionally uses only Python's standard library so it can run on a
fresh packaged EVA target without installing requests.
"""

from __future__ import annotations

import argparse
import json
import sys
import threading
import time
import urllib.error
import urllib.request
from typing import Any


def join_url(base_url: str, path: str) -> str:
    return base_url.rstrip("/") + "/" + path.lstrip("/")


def request_json(base_url: str, method: str, path: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
    body = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        body = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(join_url(base_url, path), data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
            return json.loads(raw) if raw.strip() else {}
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", errors="replace")
        try:
            parsed = json.loads(raw)
        except json.JSONDecodeError:
            parsed = {"raw": raw}
        parsed["http_status"] = exc.code
        raise RuntimeError(json.dumps(parsed, ensure_ascii=False)) from exc


def wait_for_ready(base_url: str, mode: str | None, timeout_s: float) -> dict[str, Any]:
    deadline = time.monotonic() + max(0.0, timeout_s)
    last_state: dict[str, Any] = {}
    while True:
        last_state = request_json(base_url, "GET", "/api/backend/state")
        state_mode = str(last_state.get("mode") or "")
        ready = bool(last_state.get("ready"))
        if ready and (not mode or state_mode == mode):
            return last_state
        if time.monotonic() >= deadline:
            return last_state
        time.sleep(1.0)


def stream_chat(base_url: str, message: str) -> tuple[int, bool]:
    payload = {"messages": [{"role": "user", "content": message}], "stream": True}
    req = urllib.request.Request(
        join_url(base_url, "/v1/chat/completions"),
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json", "Accept": "text/event-stream"},
        method="POST",
    )
    chunks = 0
    done = False
    with urllib.request.urlopen(req, timeout=300) as resp:
        for raw_line in resp:
            line = raw_line.decode("utf-8", errors="replace").strip()
            if not line.startswith("data:"):
                continue
            data = line[5:].strip()
            if data == "[DONE]":
                done = True
                break
            if data:
                chunks += 1
    return chunks, done


def stop_during_stream(base_url: str, message: str, delay_s: float) -> tuple[dict[str, Any], dict[str, Any], str | None]:
    result: dict[str, Any] = {"chunks": 0, "done": False}
    error: dict[str, str] = {}

    def worker() -> None:
        try:
            chunks, done = stream_chat(base_url, message)
            result["chunks"] = chunks
            result["done"] = done
        except Exception as exc:  # noqa: BLE001 - aborted streams often surface as transport errors
            error["message"] = str(exc)

    thread = threading.Thread(target=worker, daemon=True)
    thread.start()
    time.sleep(max(0.0, delay_s))
    stop_response = request_json(base_url, "POST", "/api/runtime/stop")
    thread.join(timeout=20)
    state = request_json(base_url, "GET", "/api/backend/state")
    result["thread_alive"] = thread.is_alive()
    result["transport_error"] = error.get("message")
    return stop_response, state, json.dumps(result, ensure_ascii=False)


def compact_state(state: dict[str, Any]) -> dict[str, Any]:
    caps = state.get("capabilities") or {}
    tts = caps.get("tts") or {}
    return {
        "state_source": state.get("state_source"),
        "mode": state.get("mode"),
        "state": state.get("state"),
        "ready": state.get("ready"),
        "bridge_available": state.get("bridge_available"),
        "current_model": state.get("current_model"),
        "endpoint": state.get("endpoint") or state.get("api_endpoint"),
        "chat_route": state.get("chat_route"),
        "capabilities": {
            "chat": caps.get("chat"),
            "stream": caps.get("stream"),
            "reset": caps.get("reset"),
            "stop": caps.get("stop"),
            "enabled_tools": caps.get("enabled_tools"),
            "configured_tools_list": caps.get("configured_tools_list"),
            "full_eva_stack": caps.get("full_eva_stack"),
            "tool_execution_route": caps.get("tool_execution_route"),
            "conversation_owner": caps.get("conversation_owner"),
            "message_input_mode": caps.get("message_input_mode"),
            "knowledge": caps.get("knowledge"),
            "mcp": caps.get("mcp"),
            "tts_model_configured": tts.get("model_configured"),
            "tts_program_available": tts.get("program_available"),
        },
    }


def validate_expected_route(state: dict[str, Any], models: dict[str, Any], expected_source: str | None, errors: list[str], label: str) -> None:
    caps = state.get("capabilities") or {}
    prefix = f"{label}: " if label else ""
    if expected_source and state.get("state_source") != expected_source:
        errors.append(f"{prefix}state_source is {state.get('state_source')!r}, expected {expected_source!r}")
    if expected_source == "bridge":
        if state.get("chat_route") != "eva_bridge":
            errors.append(f"{prefix}chat_route is {state.get('chat_route')!r}, expected 'eva_bridge'")
        if caps.get("full_eva_stack") is not True:
            errors.append(f"{prefix}bridge state does not report full_eva_stack=true")
        if caps.get("conversation_owner") != "widget":
            errors.append(f"{prefix}bridge conversation_owner is {caps.get('conversation_owner')!r}, expected 'widget'")
        if caps.get("message_input_mode") != "latest_user_text":
            errors.append(f"{prefix}bridge message_input_mode is {caps.get('message_input_mode')!r}, expected 'latest_user_text'")
        if models.get("state_source") != "bridge":
            errors.append(f"{prefix}models state_source is {models.get('state_source')!r}, expected 'bridge'")
    if expected_source == "direct_runtime":
        if state.get("chat_route") != "direct_runtime":
            errors.append(f"{prefix}chat_route is {state.get('chat_route')!r}, expected 'direct_runtime'")
        if caps.get("full_eva_stack") is True:
            errors.append(f"{prefix}direct runtime unexpectedly reports full_eva_stack=true")
        if caps.get("conversation_owner") != "acp_runtime":
            errors.append(f"{prefix}direct conversation_owner is {caps.get('conversation_owner')!r}, expected 'acp_runtime'")
        if caps.get("message_input_mode") != "request_messages":
            errors.append(f"{prefix}direct message_input_mode is {caps.get('message_input_mode')!r}, expected 'request_messages'")
        if models.get("state_source") != "direct_runtime":
            errors.append(f"{prefix}models state_source is {models.get('state_source')!r}, expected 'direct_runtime'")


def main() -> int:
    parser = argparse.ArgumentParser(description="Smoke-check EVA ACP runtime endpoints.")
    parser.add_argument("--base-url", default="http://127.0.0.1:19070")
    parser.add_argument("--expect-source", choices=("bridge", "direct_runtime", "legacy_acp"), help="Fail unless backend state_source matches this value.")
    parser.add_argument("--load-local-model", help="Local GGUF path to load through /api/backend/load.")
    parser.add_argument("--backend", help="Device backend for --load-local-model, for example auto/cpu/cuda/vulkan/opencl.")
    parser.add_argument("--load-port", help="Backend server port for --load-local-model.")
    parser.add_argument("--nctx", type=int, help="n_ctx value for --load-local-model.")
    parser.add_argument("--nthread", type=int, help="Thread count for --load-local-model.")
    parser.add_argument("--load-link-endpoint", help="Remote OpenAI-compatible base endpoint to load through /api/backend/load.")
    parser.add_argument("--load-link-model", help="Remote model id for --load-link-endpoint.")
    parser.add_argument("--load-link-key", default="", help="Remote API key for --load-link-endpoint.")
    parser.add_argument("--wait-ready", type=float, default=30.0, help="Seconds to wait for ready state after a load request.")
    parser.add_argument("--message", help="Optional chat message to send through /v1/chat/completions.")
    parser.add_argument("--stream", action="store_true", help="Use streaming chat when --message is set.")
    parser.add_argument("--reset", action="store_true", help="Call /api/runtime/reset.")
    parser.add_argument("--check-stop", action="store_true", help="Call /api/runtime/stop.")
    parser.add_argument("--stop-during-stream", action="store_true", help="Start a stream, then call /api/runtime/stop after --stop-delay seconds.")
    parser.add_argument("--stop-delay", type=float, default=2.0)
    args = parser.parse_args()

    errors: list[str] = []
    if args.load_local_model and args.load_link_endpoint:
        print("Use only one of --load-local-model or --load-link-endpoint.", file=sys.stderr)
        return 2

    try:
        health = request_json(args.base_url, "GET", "/health")
        state = request_json(args.base_url, "GET", "/api/backend/state")
        models = request_json(args.base_url, "GET", "/v1/models")
    except Exception as exc:  # noqa: BLE001 - smoke script prints concise target error
        print(f"ACP endpoint failed: {exc}", file=sys.stderr)
        return 2

    print("health:", json.dumps(health, ensure_ascii=False, indent=2))
    print("state:", json.dumps(compact_state(state), ensure_ascii=False, indent=2))
    print("models:", len(models.get("data") or []))

    caps = state.get("capabilities") or {}
    validate_expected_route(state, models, args.expect_source, errors, "initial")

    for key in ("chat", "stream", "reset", "stop"):
        if caps.get(key) is not True:
            errors.append(f"missing capability: {key}")

    if args.load_local_model:
        payload: dict[str, Any] = {"mode": "local", "model_path": args.load_local_model}
        if args.backend:
            payload["backend"] = args.backend
        if args.load_port:
            payload["port"] = args.load_port
        if args.nctx:
            payload["nctx"] = args.nctx
        if args.nthread:
            payload["nthread"] = args.nthread
        load = request_json(args.base_url, "POST", "/api/backend/load", payload)
        print("load_local:", json.dumps(load, ensure_ascii=False, indent=2))
        state = wait_for_ready(args.base_url, "local", args.wait_ready)
        models = request_json(args.base_url, "GET", "/v1/models")
        print("state_after_local_load:", json.dumps(compact_state(state), ensure_ascii=False, indent=2))
        validate_expected_route(state, models, args.expect_source, errors, "after local load")
        if state.get("mode") != "local" or not state.get("ready"):
            errors.append("local load did not reach ready local state")

    if args.load_link_endpoint:
        payload = {
            "mode": "link",
            "api_endpoint": args.load_link_endpoint,
            "api_model": args.load_link_model or "",
            "api_key": args.load_link_key,
        }
        load = request_json(args.base_url, "POST", "/api/backend/load", payload)
        print("load_link:", json.dumps(load, ensure_ascii=False, indent=2))
        state = wait_for_ready(args.base_url, "link", args.wait_ready)
        models = request_json(args.base_url, "GET", "/v1/models")
        print("state_after_link_load:", json.dumps(compact_state(state), ensure_ascii=False, indent=2))
        validate_expected_route(state, models, args.expect_source, errors, "after link load")
        if state.get("mode") != "link" or not state.get("ready"):
            errors.append("link load did not reach ready link state")

    if args.reset:
        reset = request_json(args.base_url, "POST", "/api/runtime/reset")
        print("reset:", json.dumps(reset, ensure_ascii=False, indent=2))
        state = request_json(args.base_url, "GET", "/api/backend/state")
        models = request_json(args.base_url, "GET", "/v1/models")
        print("state_after_reset:", json.dumps(compact_state(state), ensure_ascii=False, indent=2))
        validate_expected_route(state, models, args.expect_source, errors, "after reset")

    if args.check_stop:
        stop = request_json(args.base_url, "POST", "/api/runtime/stop")
        print("stop:", json.dumps(stop, ensure_ascii=False, indent=2))
        state = request_json(args.base_url, "GET", "/api/backend/state")
        models = request_json(args.base_url, "GET", "/v1/models")
        print("state_after_stop:", json.dumps(compact_state(state), ensure_ascii=False, indent=2))
        validate_expected_route(state, models, args.expect_source, errors, "after stop")

    if args.stop_during_stream:
        stop_message = args.message or "Please write a long response slowly so I can test stop."
        stop_response, stop_state, stop_result = stop_during_stream(args.base_url, stop_message, args.stop_delay)
        print("stop_during_stream_response:", json.dumps(stop_response, ensure_ascii=False, indent=2))
        print("stop_during_stream_result:", stop_result)
        print("state_after_stop:", json.dumps(compact_state(stop_state), ensure_ascii=False, indent=2))
        models = request_json(args.base_url, "GET", "/v1/models")
        validate_expected_route(stop_state, models, args.expect_source, errors, "after stop during stream")
        state_name = str(stop_state.get("state") or stop_state.get("phase") or "")
        if stop_state.get("is_run") is True or state_name in {"running", "tool_running"}:
            errors.append("runtime still reports running after stop")

    if args.message:
        if args.stream:
            chunks, done = stream_chat(args.base_url, args.message)
            print(f"stream_chat: chunks={chunks} done={done}")
            if chunks <= 0 or not done:
                errors.append("stream chat did not emit chunks and DONE")
        else:
            response = request_json(
                args.base_url,
                "POST",
                "/v1/chat/completions",
                {"messages": [{"role": "user", "content": args.message}], "stream": False},
            )
            content = (((response.get("choices") or [{}])[0].get("message") or {}).get("content") or "").strip()
            print("chat_content:", content)
            if not content:
                errors.append("non-stream chat returned empty content")

    if errors:
        print("FAILED:")
        for error in errors:
            print(f"- {error}")
        return 1

    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
