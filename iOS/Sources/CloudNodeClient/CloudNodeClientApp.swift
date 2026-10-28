import SwiftUI

@main
struct CloudNodeClientApp: App {
    @StateObject private var manager = ConnectionManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(manager)
        }
    }
}
