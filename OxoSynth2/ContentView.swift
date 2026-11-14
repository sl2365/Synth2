import SwiftUI

/// The app only hosts the AUv3 extension: the synth runs inside an Audio Unit
/// host. No engine, no library (the plugin's bank browser manages it).
struct ContentView: View {
    private static let panel = Color(red: 0xB7 / 255, green: 0xB7 / 255, blue: 0x9B / 255)
    private static let ink = Color(red: 0x1A / 255, green: 0x1A / 255, blue: 0x0F / 255)

    private let info = Bundle.main.infoDictionary ?? [:]
    private var name: String { info["CFBundleDisplayName"] as? String ?? "Synth2" }
    private var version: String {
        "version \(info["CFBundleShortVersionString"] as? String ?? "?") (\(info["CFBundleVersion"] as? String ?? "?"))"
    }

    var body: some View {
        ZStack {
            Self.panel.ignoresSafeArea()

            VStack(alignment: .leading, spacing: 16) {
                HStack(alignment: .lastTextBaseline, spacing: 8) {
                    Text(name)
                        .font(.system(size: 34, weight: .bold))
                    Text(version)
                        .font(.system(size: 11, weight: .medium))
                        .opacity(0.6)
                }

                VStack(alignment: .leading, spacing: 8) {
                    Text("\(name) is an Audio Unit instrument: open it in a host app like AUM, Cubasis, GarageBand or Loopy Pro.")
                        .font(.system(size: 14))
                    Text("In your host, add an AUv3 instrument and pick \(name).")
                        .font(.system(size: 13))
                        .opacity(0.7)
                }
            }
            .foregroundColor(Self.ink)
            .padding(22)
            .frame(maxWidth: 560, alignment: .leading)
        }
    }
}

#Preview {
    ContentView()
}
