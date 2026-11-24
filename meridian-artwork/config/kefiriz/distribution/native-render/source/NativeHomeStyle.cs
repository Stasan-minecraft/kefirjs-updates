using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using SARCExt;
using SwitchThemes.Common.Bflyt;

internal static class NativeHomeStyle
{
    // The four library buttons, two sharing/controller buttons and three system buttons keep their original order.
    static readonly string[] Buttons = { "LR", "Noti", "Shop", "Pvr", "Splay", "Ctrl", "Vgc", "Set", "Pow" };
    static readonly int[] Centers = { 242, 340, 438, 536, 665, 763, 895, 993, 1091 };

    static IEnumerable<int> Sections(byte[] layout)
    {
        if (layout.Length < 20 || Tag(layout, 0) != "FLYT" || BitConverter.ToUInt16(layout, 4) != 0xfeff ||
            BitConverter.ToUInt16(layout, 6) != 20 || BitConverter.ToUInt32(layout, 12) != layout.Length)
            throw new InvalidDataException("Unrecognized HOME layout header.");
        int count = BitConverter.ToUInt16(layout, 16), pos = 20;
        for (int i = 0; i < count; i++) {
            if (pos > layout.Length - 8) throw new InvalidDataException("Truncated layout section.");
            int length = BitConverter.ToInt32(layout, pos + 4);
            if (length < 8 || length > layout.Length - pos) throw new InvalidDataException("Invalid layout section.");
            string type = Tag(layout, pos);
            if (((type == "prt1" || type == "pan1" || type == "txt1" || type == "pic1") && length < 84) ||
                (type == "usd1" && length < 12))
                throw new InvalidDataException("Truncated native pane.");
            yield return pos;
            pos += length;
        }
        if (pos != layout.Length) throw new InvalidDataException("Unexpected trailing layout data.");
    }

    static string Tag(byte[] layout, int pos) => Encoding.ASCII.GetString(layout, pos, 4);
    static string Name(byte[] layout, int pos) => Encoding.ASCII.GetString(layout, pos + 12, 24).TrimEnd('\0');

    static void WriteFloat(byte[] layout, int pos, float value) =>
        Buffer.BlockCopy(BitConverter.GetBytes(value), 0, layout, pos, 4);

    public static void Arrange(SarcData archive)
    {
        const string path = "blyt/RdtBase.bflyt";
        if (!archive.Files.TryGetValue(path, out byte[] source))
            throw new InvalidDataException("Incomplete HOME export: the main control layout is missing.");
        var document = new BflytFile(source);
        var changes = new Dictionary<string, Tuple<float, float>>();
        void Move(string paneName, string parentName, string type, float oldX, float oldY, float x, float y) {
            var pane = document[paneName] as Pan1Pane;
            var parent = pane?.Parent as Pan1Pane;
            if (pane == null || parent == null || parent.PaneName != parentName || pane.name != type ||
                pane.Position.X != oldX || pane.Position.Y != oldY)
                throw new InvalidDataException("Unrecognized HOME control geometry: " + paneName);
            changes.Add(paneName, Tuple.Create(x, y));
        }
        // The strip, its touch-scroll guides, and the empty-library caption move by the same distance.
        foreach (string root in new[] { "N_GameRoot", "N_ScrollArea", "N_ScrollWindow" })
            Move(root, "N_Root", "pan1", 0, 38, 0, 61);
        Move("T_Blank", "N_Root", "txt1", 0, 197, 0, 220);
        Move("N_System", "N_Root", "pan1", -54, -184, 0, -197);
        for (int i = 0; i < Buttons.Length; i++)
            Move("L_Btn" + Buttons[i], "N_System", "prt1", -306 + i * 90, 0, Centers[i] - 640, 0);
        var result = (byte[])source.Clone();
        foreach (int pos in Sections(source)) {
            string type = Tag(source, pos);
            if (type != "prt1" && type != "pan1" && type != "txt1") continue;
            string name = Name(source, pos);
            if (!changes.TryGetValue(name, out var change)) continue;
            WriteFloat(result, pos + 44, change.Item1);
            WriteFloat(result, pos + 48, change.Item2);
            changes.Remove(name);
        }
        if (changes.Count != 0) throw new InvalidDataException("HOME positioning did not complete.");
        archive.Files[path] = result;
    }

    // Narrow just the shape of the plate, pressed state and effects. Input bounds and the cursor keep their size.
    public static void ShapeDockButton(byte[] layout) => StylePictures(layout, "dock");

    public static void StyleApplet(SarcData archive, string part)
    {
        string[] layouts;
        switch (part) {
            case "set": layouts = new[] { "BtnNav_Root" }; break;
            case "news": layouts = new[] { "NtfBtnNavChannel", "NtfBtnSquareList" }; break;
            case "apps": layouts = new[] { "FlcBtnIconGame", "FlcBtnGrp" }; break;
            default: return;
        }
        foreach (string layout in layouts) {
            if (archive.Files.TryGetValue("blyt/" + layout + ".bflyt", out var data)) StylePictures(data, part);
        }
    }

    static void StylePictures(byte[] layout, string screen)
    {
        string picture = null;
        foreach (int pos in Sections(layout)) {
            string type = Tag(layout, pos);
            if (type == "usd1" && picture != null) {
                int end = pos + BitConverter.ToInt32(layout, pos + 4);
                int count = BitConverter.ToUInt16(layout, pos + 8);
                for (int i = 0; i < count; i++) {
                    int entry = pos + 12 + i * 12;
                    if (entry > end - 12) throw new InvalidDataException("Invalid button color directory.");
                    int label = checked(entry + BitConverter.ToInt32(layout, entry));
                    if (label < pos || label >= end) throw new InvalidDataException("Invalid button property name.");
                    int nullAt = Array.IndexOf(layout, (byte)0, label, end - label);
                    if (nullAt < label) throw new InvalidDataException("Unterminated button property.");
                    string property = Encoding.ASCII.GetString(layout, label, nullAt - label);
                    if (property != "S_RoundRadius" && property != "C_W" && property != "S_BorderSize" &&
                        property != "S_BorderColorSelect0" && property != "S_DropShadowVolume") continue;
                    int value = checked(entry + BitConverter.ToInt32(layout, entry + 4));
                    int arity = BitConverter.ToUInt16(layout, entry + 8);
                    bool number = layout[entry + 10] == 2 && arity == 1;
                    bool integer = layout[entry + 10] == 1 && arity > 0;
                    if (value < pos || value > end - 4) throw new InvalidDataException("Invalid visual property.");
                    if (screen == "dock" && property == "S_RoundRadius" && number)
                        WriteFloat(layout, value, 12);
                    else if (picture == "P_Active" && property == "C_W" && integer)
                        Buffer.BlockCopy(BitConverter.GetBytes(93), 0, layout, value, 4);
                    else if (picture == "P_InnerCursor" && property == "S_BorderSize" && number)
                        WriteFloat(layout, value, 4);
                    else if (picture == "P_InnerCursor" && property == "S_BorderColorSelect0" && integer)
                        Buffer.BlockCopy(BitConverter.GetBytes(93), 0, layout, value, 4);
                    else if (picture == "P_BtnCardBase" && property == "S_DropShadowVolume" && number)
                        WriteFloat(layout, value, 0.45f);
                }
            }
            picture = null;
            if (type == "pic1") {
                string name = Name(layout, pos);
                if (screen == "dock") {
                    if (name == "P_PictBase" || name == "P_Effect" || name == "P_BtnDecideKey" ||
                        name == "P_BtnTouch") picture = name;
                } else if (name == "P_Active" || name == "P_InnerCursor" || name == "P_BtnCardBase") {
                    picture = name;
                    if (name == "P_Active") WriteFloat(layout, pos + 76, screen == "set" ? 10 : 8);
                }
            }
        }
    }
}
