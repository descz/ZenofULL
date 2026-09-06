from playwright.sync_api import sync_playwright
from pathlib import Path


def main():
    errors = []
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1440, "height": 900}, device_scale_factor=1)
        page.on("pageerror", lambda error: errors.append(f"pageerror: {error}"))
        page.on("console", lambda message: errors.append(f"console {message.type}: {message.text}") if message.type == "error" else None)
        page.goto((Path.cwd() / "index.html").as_uri())
        page.wait_for_load_state("networkidle")
        page.wait_for_timeout(500)
        page.screenshot(path="qa-initial.png")

        # Removed surfaces are really gone.
        assert page.locator('button[data-workspace="memory"]').count() == 1
        assert page.locator('button[data-workspace="marketplace"]').count() == 0
        assert page.locator('button[data-workspace="squad"]').count() == 0
        assert page.locator('button[data-workspace="projects"]').count() == 0
        assert page.locator('.zeno-sidebar-label').count() == 0
        assert page.locator('.zeno-sidebar-section-title').count() == 0
        assert page.locator('.oc-vibrancy-pill').count() == 0
        assert page.get_by_text("Marketplace", exact=True).count() == 0
        assert page.get_by_text("Squad", exact=True).count() == 0
        assert page.get_by_text("About Zeno", exact=True).count() == 0
        assert page.get_by_text("Project tools", exact=True).count() == 0

        # "My Projects" label replaced "Project tools".
        tools_label = page.locator('.zeno-project-tools span').first
        assert tools_label.text_content().strip() == "My Projects"

        # New chat and Settings still work.
        assert page.locator('button[data-action="new-session"]').count() == 1
        page.locator('[data-sidebar-root] [aria-label="Settings"]').click()
        page.locator('[data-settings-backdrop]').wait_for()
        page.keyboard.press("Escape")
        page.locator('[data-settings-backdrop]').wait_for(state="detached")

        # Create a project from the "+" tool: dialog opens, submit lands in chat.
        page.locator('[data-sidebar-root] .zeno-sidebar-tool[data-action="add-project"]').click()
        page.locator(".project-dialog").wait_for()
        page.locator("[data-project-name]").fill("Zeno cockpit")
        page.locator('[data-project-folder]').nth(0).check()
        page.locator('[data-project-folder]').nth(2).check()
        assert page.locator("[data-project-folder-count]").inner_text() == "2 selected"
        page.locator("[data-project-form]").locator('button[type="submit"]').click()
        page.locator("[data-chat-input]").wait_for()
        assert page.get_by_text("Zeno cockpit", exact=True).count() >= 1

        # Sidebar project row keeps its folder icon in mandatory white.
        page.locator('[data-sidebar-root] .zeno-project-open').first.wait_for()
        icon_color = page.locator('[data-sidebar-root] .zeno-project-open .zeno-sidebar-nav-icon').first.evaluate(
            "el => getComputedStyle(el).color"
        )
        assert icon_color == "rgb(255, 255, 255)", f"folder icon color is {icon_color}"
        page.screenshot(path="qa-projects.png")

        # Memory page still intact.
        page.locator('button[data-workspace="memory"]').click()
        page.locator(".memory-view").wait_for()
        assert page.get_by_text("Zeno Agent Memory", exact=True).count() == 1
        assert page.locator(".memory-map-canvas").count() == 1
        page.screenshot(path="qa-memory.png")

        # Clicking a chat returns to the chat view.
        page.locator('[data-sidebar-root] [data-session-id]').first.click()
        page.locator("[data-chat-input]").wait_for()

        mobile = browser.new_page(viewport={"width": 390, "height": 844}, device_scale_factor=1)
        mobile.goto((Path.cwd() / "index.html").as_uri())
        mobile.wait_for_timeout(700)
        mobile.locator('button[data-workspace="memory"]').click()
        mobile.locator(".memory-view").wait_for()
        mobile.locator(".memory-node").first.locator(".memory-node-shape").dblclick()
        note_box = mobile.locator(".memory-note-dialog").bounding_box()
        assert note_box and note_box["width"] <= 390 and note_box["height"] <= 844
        mobile.close()
        browser.close()
    if errors:
        raise AssertionError("\n".join(errors))
    print("UI smoke test passed")


if __name__ == "__main__":
    main()
