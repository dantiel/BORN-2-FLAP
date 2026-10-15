# frozen_string_literal: true

module Born2Flap
  module UI
    # Composes the live cockpit + radio base layer with whichever modal
    # overlays are open (menu / poi / settings / rc / channels / splash) into a
    # single neutral tree for the reconciler. Pure function of telemetry — the
    # Brain is stateless; C++ telemetry is the single source of truth and every
    # user action is routed back to C++ game logic, then reflected here on the
    # next tick.
    #
    # The menu is a single paged surface: a persistent left nav rail plus one
    # content page at a time (OPEN WORLDS / FLIGHT DESK / APP SETTINGS / RC),
    # selected by C++ telemetry `nav_page`. This is what keeps the world
    # selector from ever being rendered on top of the other pages.
    module Dashboard
      module_function

      # Screens owned by the Brain's Router that show the menu (or a menu-like
      # front-end) instead of the in-flight HUD.
      MENU_SCREENS = %i[boot main_menu hangar settings quit].freeze

      # Top-level menu pages. Order == nav_page index. Each entry maps the nav
      # action (handled in C++) to the i18n key for its rail label.
      NAV_ITEMS = [
        { action: 'menu.open_worlds', key: 'menu.open_worlds' },
        { action: 'menu.settings',    key: 'menu.flight_desk' },
        { action: 'menu.preferences', key: 'menu.app_settings' },
        { action: 'menu.controls',    key: 'menu.rc_transmitter' }
      ].freeze

      def fit(tree, width, height)
        Node.new(:scalebox, {}, [Node.new(:sizebox, { width: width, height: height }, [tree])])
      end

      def build(telemetry, screen: :free_flight)
        telemetry = {} unless telemetry.is_a?(Hash)
        # While the C++ level-loading screen is up, render nothing here: the
        # movie player owns the viewport and the HUD/menu must not bleed through.
        return Node.new(:overlay, { visible: false }, []) if telemetry[:ui_hidden] || telemetry[:loading]
        children = []

        if MENU_SCREENS.include?(screen.to_sym)
          children << Node.new(:image, {texture: "/Game/Splash/born2flap-splash-new.born2flap-splash-new"})
          children << menu_frame(telemetry)
        else
          menu_open = telemetry[:menu] ? true : false
          if menu_open
            # In-level F10 menu: the paged menu replaces the live HUD/radio and
            # the standalone panels (they are its pages now).
            children << menu_frame(telemetry)
          else
            children << Views::FlightHud.render(:cockpit, telemetry) if telemetry[:hud] != false
            children << Views::Radio.render(:panel, telemetry) if telemetry[:radio_visible] != false && telemetry[:hud] != false
            children << Views::Channels.render(:panel, telemetry) if telemetry[:channels] && telemetry[:hud] != false
            children << Views::Rc.render(:panel, telemetry) if telemetry[:rc]
            children << Views::Poi.render(:panel, telemetry) if telemetry[:poi]
            children << fit(Views::Preferences.render(:panel, telemetry), 1050, 800) if telemetry[:preferences]
            children << fit(Views::TuningEditor.render(:editor, telemetry), 1050, 780) if telemetry[:settings] && !telemetry[:preferences]
          end
        end

        children << Views::Splash.render(:panel, telemetry) if telemetry[:splash]
        Node.new(:overlay, {}, children)
      end

      # The fullscreen menu frame: persistent left nav rail + one content page.
      def menu_frame(telemetry)
        nav = (telemetry[:nav_page] || 0).to_i.clamp(0, NAV_ITEMS.size - 1)
        content = case nav
                  when 0 then Views::Menu.render(:panel, telemetry)
                  when 1 then Views::TuningEditor.render(:editor, telemetry)
                  when 2 then Views::Preferences.render(:panel, telemetry)
                  else        Views::Rc.render(:panel, telemetry)
                  end
        fit(Node.new(:hbox, { spacing: 24 }, [
          Node.new(:sizebox, { width: 280 }, [menu_sidebar(telemetry, nav)]),
          Node.new(:sizebox, { width: 1050, height: 800 }, [content])
        ]), 1420, 800)
      end

      def menu_sidebar(telemetry, nav)
        lang = telemetry[:lang] || I18n::DEFAULT
        inlevel = telemetry[:menu_inlevel] ? true : false
        buttons = NAV_ITEMS.each_with_index.map do |item, index|
          Node.new(:button, {
            label: I18n.t(item[:key], lang: lang),
            action: item[:action],
            tone: (index == nav ? 'ghost-active' : 'ghost')
          })
        end
        nodes = [
          Node.new(:sizebox, { height: 90 }, [Node.new(:image, { texture: '/Game/Splash/born2flap-logo.born2flap-logo' })]),
          Node.new(:textblock, { text: I18n.t('menu.subtitle', lang: lang), wrap: true, size: 's' })
        ] + buttons
        nodes << Node.new(:button, { label: I18n.t('menu.resume_flight', lang: lang), action: 'menu.resume', tone: 'good' }) if inlevel
        nodes << Node.new(:button, { label: I18n.t('menu.quit', lang: lang), action: 'menu.quit' })
        Node.new(:panel, { padding: 20 }, [Node.new(:vbox, { spacing: 14 }, nodes)])
      end
    end
  end
end
