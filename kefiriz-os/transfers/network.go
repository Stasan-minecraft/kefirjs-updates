package main

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"

	"github.com/godbus/dbus/v5"
)

const cm = "net.connman"
const agentPath = dbus.ObjectPath("/org/kefiriz/NetworkAgent")

type serviceInfo struct {
	Path  dbus.ObjectPath
	Props map[string]dbus.Variant
}
type wifiAgent struct {
	lock     sync.Mutex
	bus      *dbus.Conn
	owner    dbus.Sender
	service  dbus.ObjectPath
	password string
}

func (a *wifiAgent) Release() *dbus.Error                                           { return nil }
func (a *wifiAgent) Cancel() *dbus.Error                                            { return nil }
func (a *wifiAgent) ReportError(service dbus.ObjectPath, reason string) *dbus.Error { return nil }
func (a *wifiAgent) RequestBrowser(service dbus.ObjectPath, url string) *dbus.Error {
	return dbus.NewError(cm+".Agent.Error.Canceled", nil)
}
func connmanOwner(ctx context.Context, bus *dbus.Conn) (dbus.Sender, error) {
	call, cancel := context.WithTimeout(ctx, 3*time.Second)
	defer cancel()
	var owner string
	err := bus.BusObject().CallWithContext(call, "org.freedesktop.DBus.GetNameOwner", 0, cm).Store(&owner)
	return dbus.Sender(owner), err
}

func (a *wifiAgent) RequestInput(sender dbus.Sender, service dbus.ObjectPath, fields map[string]dbus.Variant) (map[string]dbus.Variant, *dbus.Error) {
	if sender != a.owner {
		return nil, dbus.NewError(cm+".Agent.Error.Canceled", nil)
	}
	// Verify live ownership as well; a NameOwnerChanged signal could still be in transit.
	current, err := connmanOwner(context.Background(), a.bus)
	a.lock.Lock()
	defer a.lock.Unlock()
	if err != nil || current != sender || a.service == "" || service != a.service {
		return nil, dbus.NewError(cm+".Agent.Error.Canceled", nil)
	}
	values := map[string]dbus.Variant{}
	if _, ok := fields["Passphrase"]; ok {
		values["Passphrase"] = dbus.MakeVariant(a.password)
	}
	return values, nil
}
func (a *wifiAgent) set(path dbus.ObjectPath, pass string) {
	a.lock.Lock()
	defer a.lock.Unlock()
	a.service = path
	a.password = pass
}
func prop(p map[string]dbus.Variant, k string) string { v, _ := p[k].Value().(string); return v }
func wireless(session context.Context, bus *dbus.Conn, owner dbus.Sender) ([]serviceInfo, error) {
	var out []serviceInfo
	ctx, cancel := context.WithTimeout(session, 3*time.Second)
	defer cancel()
	err := bus.Object(string(owner), "/").CallWithContext(ctx, cm+".Manager.GetServices", 0).Store(&out)
	return out, err
}
func visible(s string) string {
	return strings.Map(func(r rune) rune {
		if r < 32 || r == 127 {
			return ' '
		}
		return r
	}, s)
}
func runNetwork(ctx context.Context) error {
	dir := folder("KEFIRIZ_NETWORK_DIR", "/run/kefiriz-network")
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}
	if err := os.Chmod(dir, 0755); err != nil {
		return err
	}
	publish := func(name, msg string) {
		path := filepath.Join(dir, name)
		if b, e := os.ReadFile(path); e == nil && string(b) == msg {
			return
		}
		if atomicFile(path, []byte(msg)) == nil {
			os.Chmod(path, 0644)
		}
	}
	socket := filepath.Join(dir, "control")
	if st, err := os.Lstat(socket); err == nil && st.Mode()&os.ModeSocket != 0 {
		os.Remove(socket)
	}
	ln, err := net.Listen("unix", socket)
	if err != nil {
		return err
	}
	defer ln.Close()
	if err = os.Chmod(socket, 0600); err != nil {
		return err
	}
	if err = os.Chown(socket, 1000, 1000); err != nil {
		return err
	}
	bus, err := dbus.ConnectSystemBus()
	if err != nil {
		return err
	}
	defer bus.Close()
	session, endSession := context.WithCancelCause(ctx)
	defer endSession(nil)
	changes := make(chan *dbus.Signal, 8)
	bus.Signal(changes)
	defer bus.RemoveSignal(changes)
	const daemon = "org.freedesktop.DBus"
	if err = bus.AddMatchSignal(dbus.WithMatchSender(daemon), dbus.WithMatchInterface(daemon),
		dbus.WithMatchObjectPath("/org/freedesktop/DBus"), dbus.WithMatchMember("NameOwnerChanged"),
		dbus.WithMatchArg(0, cm)); err != nil {
		return err
	}
	owner, err := connmanOwner(session, bus)
	if err != nil {
		return err
	}
	agent := &wifiAgent{bus: bus, owner: owner}
	ownerChanged := errors.New("ConnMan ownership changed; restarting network service")
	go func() {
		for {
			select {
			case <-session.Done():
				return
			case sig, ok := <-changes:
				if !ok || (sig.Sender == daemon && sig.Name == daemon+".NameOwnerChanged" &&
					len(sig.Body) == 3 && sig.Body[0] == cm && sig.Body[1] == string(owner)) {
					agent.set("", "")
					endSession(ownerChanged)
					return
				}
			}
		}
	}()
	if err = bus.Export(agent, agentPath, cm+".Agent"); err != nil {
		return err
	}
	manager := bus.Object(string(owner), "/")
	reg, finishRegistration := context.WithTimeout(session, 3*time.Second)
	er := manager.CallWithContext(reg, cm+".Manager.RegisterAgent", 0, agentPath).Err
	finishRegistration()
	if er != nil {
		return er
	}
	defer func() {
		agent.set("", "")
		cleanup, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		manager.CallWithContext(cleanup, cm+".Manager.UnregisterAgent", 0, agentPath)
	}()
	requests := make(chan string, 1)
	go func() {
		for {
			c, e := ln.Accept()
			if e != nil {
				return
			}
			c.SetReadDeadline(time.Now().Add(time.Second))
			line, e := bufio.NewReaderSize(c, 512).ReadSlice('\n')
			if e == nil {
				select {
				case requests <- strings.TrimSuffix(string(line), "\n"):
				default:
				}
			}
			c.Close()
		}
	}()
	refresh := func() {
		items, e := wireless(session, bus, owner)
		if e != nil {
			publish("status", "Network service unavailable")
			return
		}
		var out strings.Builder
		for _, s := range items {
			if prop(s.Props, "Type") != "wifi" {
				continue
			}
			name := visible(prop(s.Props, "Name"))
			if name == "" {
				continue
			}
			security, _ := s.Props["Security"].Value().([]string)
			mode := "unsupported"
			for _, v := range security {
				if v == "none" {
					mode = "open"
					break
				}
				if v == "psk" || v == "sae" {
					mode = "password"
					break
				}
			}
			fmt.Fprintf(&out, "%s\t%s\t%s\t%s\n", filepath.Base(string(s.Path)), name, mode, prop(s.Props, "State"))
		}
		publish("networks.tsv", out.String())
	}
	refresh()
	tick := time.NewTicker(5 * time.Second)
	defer tick.Stop()
	serviceID := regexp.MustCompile("^wifi_[0-9A-Za-z_]+$")
	wifi := bus.Object(string(owner), "/net/connman/technology/wifi")
	for {
		select {
		case <-session.Done():
			if errors.Is(context.Cause(session), ownerChanged) {
				return ownerChanged
			}
			return nil
		case <-tick.C:
			refresh()
		case request := <-requests:
			args := strings.SplitN(request, "\t", 3)
			call, cancel := context.WithTimeout(session, 50*time.Second)
			if request == "scan" {
				publish("status", "Scanning for Wi-Fi...")
				wifi.CallWithContext(call, cm+".Technology.SetProperty", 0, "Powered", dbus.MakeVariant(true))
				if err := wifi.CallWithContext(call, cm+".Technology.Scan", 0).Err; err != nil {
					publish("status", "Scan unavailable. Try again.")
				} else {
					publish("status", "Choose a network")
				}
			} else if len(args) == 3 && args[0] == "connect" && serviceID.MatchString(args[1]) && len(args[2]) <= 64 {
				path := dbus.ObjectPath("/net/connman/service/" + args[1])
				items, e := wireless(session, bus, owner)
				found := false
				for _, s := range items {
					if s.Path == path && prop(s.Props, "Type") == "wifi" {
						found = true
					}
				}
				if e == nil && found {
					publish("status", "Connecting...")
					agent.set(path, args[2])
					er := bus.Object(string(owner), path).CallWithContext(call, cm+".Service.Connect", 0).Err
					agent.set("", "")
					if er != nil {
						publish("status", "Connection failed. Check the password and try again.")
					} else {
						publish("status", "Connected")
					}
				}
			}
			cancel()
			refresh()
		}
	}
}
