//go:build linux

package main

import (
	"bufio"
	"bytes"
	"context"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/godbus/dbus/v5"
)

func TestAgentRequiresRegisteredSender(t *testing.T) {
	agent := &wifiAgent{owner: dbus.Sender(":1.10")}
	values, err := agent.RequestInput(dbus.Sender(":1.11"), "/net/connman/service/wifi_ap1", nil)
	if err == nil || len(values) != 0 {
		t.Fatal("agent accepted an unregistered caller")
	}
}

func TestNetworkPrivateBus(t *testing.T) {
	if os.Geteuid() != 0 && (os.Geteuid() != 1000 || os.Getegid() != 1000) {
		t.Skip("the production control socket belongs to UID/GID 1000")
	}
	daemon, err := exec.LookPath("dbus-daemon")
	if err != nil {
		t.Skip("dbus-daemon unavailable")
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	cmd := exec.CommandContext(ctx, daemon, "--session", "--nofork", "--print-address=1", "--address=unix:tmpdir=/tmp")
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		t.Fatal(err)
	}
	if err = cmd.Start(); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { cancel(); _ = cmd.Wait() })
	address, err := bufio.NewReader(stdout).ReadString('\n')
	if err != nil {
		t.Fatal(err)
	}
	t.Setenv("DBUS_SYSTEM_BUS_ADDRESS", strings.TrimSpace(address))
	dir := t.TempDir()
	t.Setenv("KEFIRIZ_NETWORK_DIR", dir)
	bus, err := dbus.Connect(strings.TrimSpace(address))
	if err != nil {
		t.Fatal(err)
	}
	defer bus.Close()
	const service = dbus.ObjectPath("/net/connman/service/wifi_ap1")
	const pass = "test-only-coral"
	var mu sync.Mutex
	var registered dbus.Sender
	var powered, accepted bool
	var reads, scans, connects, unregisters atomic.Int32
	export := func(path dbus.ObjectPath, iface string, methods map[string]any) {
		t.Helper()
		if err := bus.ExportMethodTable(methods, path, cm+"."+iface); err != nil {
			t.Fatal(err)
		}
	}
	export("/", "Manager", map[string]any{
		"RegisterAgent": func(sender dbus.Sender, path dbus.ObjectPath) *dbus.Error {
			if path != agentPath {
				return dbus.NewError(cm+".Error.InvalidArguments", nil)
			}
			mu.Lock()
			registered = sender
			mu.Unlock()
			return nil
		},
		"UnregisterAgent": func(path dbus.ObjectPath) *dbus.Error { unregisters.Add(1); return nil },
		"GetServices": func() ([]serviceInfo, *dbus.Error) {
			reads.Add(1)
			mu.Lock()
			online := accepted
			mu.Unlock()
			state := "idle"
			if online {
				state = "online"
			}
			return []serviceInfo{
				{service, map[string]dbus.Variant{"Type": dbus.MakeVariant("wifi"), "Name": dbus.MakeVariant("Home\tWi-Fi"), "Security": dbus.MakeVariant([]string{"psk"}), "State": dbus.MakeVariant(state)}},
				{"/net/connman/service/ethernet_1", map[string]dbus.Variant{"Type": dbus.MakeVariant("ethernet")}},
			}, nil
		},
	})
	export("/net/connman/technology/wifi", "Technology", map[string]any{
		"SetProperty": func(name string, v dbus.Variant) *dbus.Error {
			mu.Lock()
			powered = name == "Powered" && v.Value() == true
			mu.Unlock()
			return nil
		},
		"Scan": func() *dbus.Error { scans.Add(1); return nil },
	})
	requestInput := func() (map[string]dbus.Variant, error) {
		mu.Lock()
		peer := registered
		mu.Unlock()
		call, stop := context.WithTimeout(ctx, 2*time.Second)
		defer stop()
		var values map[string]dbus.Variant
		fields := map[string]dbus.Variant{"Passphrase": dbus.MakeVariant(map[string]dbus.Variant{"Requirement": dbus.MakeVariant("mandatory")})}
		e := bus.Object(string(peer), agentPath).CallWithContext(call, cm+".Agent.RequestInput", 0, service, fields).Store(&values)
		return values, e
	}
	export(service, "Service", map[string]any{"Connect": func() *dbus.Error {
		connects.Add(1)
		v, e := requestInput()
		if e != nil || v["Passphrase"].Value() != pass {
			return dbus.NewError(cm+".Error.Failed", nil)
		}
		mu.Lock()
		accepted = true
		mu.Unlock()
		return nil
	}})
	own := func() {
		t.Helper()
		result, e := bus.RequestName(cm, dbus.NameFlagDoNotQueue)
		if e != nil || result != dbus.RequestNameReplyPrimaryOwner {
			t.Fatalf("could not own simulated manager: %v", e)
		}
	}
	own()
	done := make(chan error, 1)
	go func() { done <- runNetwork(ctx) }()
	wait := func(reason string, ok func() bool) {
		t.Helper()
		deadline := time.Now().Add(4 * time.Second)
		for time.Now().Before(deadline) {
			if ok() {
				return
			}
			select {
			case e := <-done:
				t.Fatalf("worker stopped before %s: %v", reason, e)
			default:
			}
			time.Sleep(5 * time.Millisecond)
		}
		t.Fatal("timeout waiting for " + reason)
	}
	fileIs := func(name, want string) bool {
		b, e := os.ReadFile(filepath.Join(dir, name))
		return e == nil && string(b) == want
	}
	send := func(line string) {
		t.Helper()
		c, e := net.DialTimeout("unix", filepath.Join(dir, "control"), time.Second)
		if e != nil {
			t.Fatal(e)
		}
		if _, e = c.Write([]byte(line + "\n")); e != nil {
			t.Fatal(e)
		}
		c.Close()
	}
	wait("initial network list", func() bool { return fileIs("networks.tsv", "wifi_ap1\tHome Wi-Fi\tpassword\tidle\n") })
	send("scan")
	wait("scan result", func() bool { return fileIs("status", "Choose a network") })
	mu.Lock()
	on := powered
	mu.Unlock()
	if !on || scans.Load() != 1 {
		t.Fatal("Wi-Fi technology did not receive power and scan")
	}
	for _, id := range []string{"/not/a/service", "wifi_missing"} {
		before := reads.Load()
		minReads := int32(1)
		if id == "wifi_missing" {
			minReads = 2
		}
		send("connect\t" + id + "\tunused")
		wait("rejected network selection", func() bool { return reads.Load() >= before+minReads })
		if connects.Load() != 0 {
			t.Fatal("unlisted network connected")
		}
	}
	send("connect\twifi_ap1\t" + pass)
	wait("connection and state refresh", func() bool {
		return fileIs("status", "Connected") && fileIs("networks.tsv", "wifi_ap1\tHome Wi-Fi\tpassword\tonline\n")
	})
	if connects.Load() != 1 {
		t.Fatal("wrong service connection count")
	}
	if _, e := requestInput(); e == nil {
		t.Fatal("agent retained a password after Connect")
	}
	all, err := os.ReadDir(dir)
	if err != nil {
		t.Fatal(err)
	}
	for _, entry := range all {
		if entry.Type().IsRegular() {
			b, e := os.ReadFile(filepath.Join(dir, entry.Name()))
			if e != nil {
				t.Fatal(e)
			}
			if bytes.Contains(b, []byte(pass)) {
				t.Fatal("plaintext credential persisted")
			}
		}
	}
	if _, err = bus.ReleaseName(cm); err != nil {
		t.Fatal(err)
	}
	select {
	case err = <-done:
		if err == nil {
			t.Fatal("name loss must trigger supervisor restart")
		}
	case <-time.After(4 * time.Second):
		t.Fatal("agent remained registered after name loss")
	}
	if unregisters.Load() != 1 {
		t.Fatal("previous manager was not unregistered")
	}
	// Simulate systemd's Restart=on-failure by launching a fresh session after the owner returns.
	own()
	before := reads.Load()
	go func() { done <- runNetwork(ctx) }()
	wait("re-registration", func() bool { return reads.Load() > before })
	cancel()
	select {
	case err = <-done:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(4 * time.Second):
		t.Fatal("worker did not stop")
	}
}
