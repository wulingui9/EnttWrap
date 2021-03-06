#pragma once
#include "entt/entt.hpp"

namespace entt {

    typedef double TimeDelta;
    using entity_type = DefaultRegistry::entity_type;
    class EntityManager;
    class EventManager;
    class Entity;

    template <typename C>
    class ComponentHandle {
    public:
        typedef C ComponentType;

        ComponentHandle() : manager_(nullptr) {}

        bool valid() const;
        operator bool() const;

        C *operator -> ();
        const C *operator -> () const;

        C &operator * ();
        const C &operator * () const;

        C *get();
        const C *get() const;

        void remove();

        Entity entity();

        bool operator == (const ComponentHandle<C> &other) const {
            return manager_ == other.manager_ && id_ == other.id_;
        }

        bool operator != (const ComponentHandle<C> &other) const {
            return !(*this == other);
        }

        ComponentHandle(EntityManager *manager, entity_type id) :
            manager_(manager), id_(id) {}

    private:
        friend class EntityManager;
        EntityManager *manager_;
        entity_type id_;
    };

    class Entity {
    public:
        Entity() = default;

        Entity(entity_type id, EntityManager* manager) :
            id_(id), manager_(manager) {}

        Entity(const Entity &other) {
            id_ = other.id_;
            manager_ = other.manager_;
        };
        Entity& operator= (const Entity &other) {
            this->id_ = other.id_;
            this->manager_ = other.manager_;
            return *this;
        };

        friend class EntityManager;

        static const entity_type INVALID = 0xffffffff;

        entity_type id()  const;

        bool valid() const;

        template <typename Component, typename ... Args>
        ComponentHandle<Component> assign(Args && ... args);

        template <typename Component>
        ComponentHandle<Component> component() const;

        template <typename Component>
        Component& componentRaw() const;

        template <typename Component>
        void remove();

        template <typename Component>
        bool has_component() const;

        void destroy();



        bool operator == (const Entity &other) const {
            return other.manager_ == manager_ && other.id_ == id_;
        }

        bool operator != (const Entity &other) const {
            return !(other == *this);
        }

        bool operator < (const Entity &other) const {
            return other.id_ < id_;
        }

    private:
        entity_type id_ = INVALID;
        EntityManager* manager_ = nullptr;
    };

    template <typename Component>
    struct ComponentAddedEvent {
        ComponentAddedEvent(Entity& entity, ComponentHandle<Component>& component) :
            entity(entity), component(component) {}
        Entity& entity;
        ComponentHandle<Component>& component;

        typedef Component ComponentType;

    };


    template <typename Component>
    struct ComponentRemovedEvent {
        ComponentRemovedEvent(Entity& entity, ComponentHandle<Component>& component) :
            entity(entity), component(component) {}
        Entity& entity;
        ComponentHandle<Component>& component;

        typedef Component ComponentType;
    };

    class EventManager
    {
    public:
        EventManager() {
            dispatcher = std::make_shared<entt::Dispatcher>();
        }

        std::shared_ptr<entt::Dispatcher> dispatcher;


        //---------------------------system专用--------------------------
        template <typename Event, typename Receiver>
        void subscribe(Receiver& receiver)
        {

            dispatcher->sink<Event>().connect(&receiver);
        }


        template <typename Event, typename Receiver>
        void unsubscribe(Receiver& receiver)
        {
            dispatcher->sink<Event>().disconnect(&receiver);
        }


        //---------------------------system专用--------------------------
        template <typename Event>
        void emit(const Event &event)
        {
            dispatcher->trigger<Event>(event);
        }

        template <typename Event, typename ... Args>
        void emit(Args && ... args) {
            Event event = Event(std::forward<Args>(args) ...);
            dispatcher->trigger<Event>(event);
        }

        //放到队列
        template <typename Event>
        void enqueue(const Event &event)
        {
            dispatcher->enqueue<Event>(event);
        }

        void update()
        {
            dispatcher->update();
        }
    };
    class EntityManager :public DefaultRegistry
    {
        using view_type = SparseSet<std::uint32_t>;
        using iterator_type = typename view_type::iterator_type;
        class EntityIter
        {
        public:
            EntityIter(EntityManager& entityManager, iterator_type& iter)
                : iter_(iter)
                , entity_manager_(entityManager) {
            }
            bool operator!= (const EntityIter& other) const
            {
                bool b = (iter_ != other.iter_);
                return b;
            }
            Entity operator* () const {
                return entity_manager_.getEntity(*iter_);
            };
            const EntityIter& operator++ ()
            {
                ++iter_;
                return *this;
            }
        private:
            iterator_type iter_;
            EntityManager& entity_manager_;
        };

        template<typename En, typename ... Components>
        class EntityViewMulti {
        public:
            EntityViewMulti(EntityManager& entityManager) noexcept
                : entity_manager_(entityManager),
                _views(entityManager.view<Components ...>(entt::persistent_t{}))
            {
            }
            EntityIter begin() {
                return EntityIter(entity_manager_, _views.begin());
            }
            EntityIter end() {
                return EntityIter(entity_manager_, _views.end());
            }
            PersistentView<En, Components...>  _views;
            EntityManager& entity_manager_;
        };

        template<typename En, typename Component>
        class EntityViewSingle {
        public:
            EntityViewSingle(EntityManager& entityManager) noexcept
                : entity_manager_(entityManager),
                _views(entityManager.view<Component>())
            {
            }
            EntityIter begin() {
                return EntityIter(entity_manager_, _views.begin());
            }
            EntityIter end() {
                return EntityIter(entity_manager_, _views.end());
            }
            View<En, Component>  _views;
            EntityManager& entity_manager_;
        };

        template<typename En, typename ... Components>
        class EntityComponentViewMulti {
            class EntityComponentIterMulti
            {
            public:
                template <typename En, typename ... Components>
                EntityComponentIterMulti(iterator_type& iter, EntityComponentViewMulti<En, Components...>& views)
                    : iter_(iter)
                    , views_(views)
                {
                }
                bool operator!= (const EntityComponentIterMulti& other) const
                {
                    bool b = (iter_ != other.iter_);
                    return b;
                }
                Entity operator* () const {
                    return views_.getEntity(*iter_);
                };
                const EntityComponentIterMulti& operator++ ()
                {
                    ++iter_;
                    return *this;
                }
            private:
                iterator_type iter_;
                EntityComponentViewMulti<En, Components...>& views_;
            };
        public:
            EntityComponentViewMulti(EntityManager& entityManager, ComponentHandle<Components> & ... handles) noexcept
                : entity_manager_(entityManager),
                _views(entityManager.view<Components ...>(entt::persistent_t{})),
                handles(std::tuple<ComponentHandle<Components> & ...>(handles...)) {
            }
            EntityComponentIterMulti begin() {
                return EntityComponentIterMulti(_views.begin(), *this);
            }
            EntityComponentIterMulti end() {
                return EntityComponentIterMulti(_views.end(), *this);
            }

            void unpack(Entity entity) const {
                unpack_<0, Components...>(entity);
            }

            template <int N, typename C>
            void unpack_(Entity& entity) const {
                std::get<N>(handles) = entity.component<C>();
            }
            template <int N, typename C0, typename C1, typename ... Cn>
            void unpack_(Entity& entity) const {
                std::get<N>(handles) = entity.component<C0>();
                unpack_<N + 1, C1, Cn...>(entity);
            }
            Entity getEntity(entity_type entityId) const {
                Entity entity = entity_manager_.getEntity(entityId);
                unpack(entity);
                return entity;
            }
            PersistentView<En, Components...>  _views;
            EntityManager& entity_manager_;
            std::tuple<ComponentHandle<Components> & ...> handles;
        };

        template<typename En, typename ... Components>
        class EntityComponentViewSingle {
            class EntityComponentIterSingle
            {
            public:
                template <typename En, typename ... Components>
                EntityComponentIterSingle(iterator_type& iter, EntityComponentViewSingle<En, Components...>& views)
                    : iter_(iter)
                    , views_(views)
                {
                }
                bool operator!= (const EntityComponentIterSingle& other) const
                {
                    bool b = (iter_ != other.iter_);
                    return b;
                }
                Entity operator* () const {
                    return views_.getEntity(*iter_);
                };
                const EntityComponentIterSingle& operator++ ()
                {
                    ++iter_;
                    return *this;
                }
            private:
                iterator_type iter_;
                EntityComponentViewSingle<En, Components...>& views_;
            };
        public:
            EntityComponentViewSingle(EntityManager& entityManager, ComponentHandle<Components> & ... handles) noexcept
                : entity_manager_(entityManager),
                _views(entityManager.view<Components ...>()),
                handles(std::tuple<ComponentHandle<Components> & ...>(handles...)) {
            }
            EntityComponentIterSingle begin() {
                return EntityComponentIterSingle(_views.begin(), *this);
            }
            EntityComponentIterSingle end() {
                return EntityComponentIterSingle(_views.end(), *this);
            }
            void unpack(Entity entity) const {
                unpack_<0, Components...>(entity);
            }
            template <int N, typename C>
            void unpack_(Entity& entity) const {
                std::get<N>(handles) = entity.component<C>();
            }
            template <int N, typename C0, typename C1, typename ... Cn>
            void unpack_(Entity& entity) const {
                std::get<N>(handles) = entity.component<C0>();
                unpack_<N + 1, C1, Cn...>(entity);
            }
            Entity getEntity(entity_type entityId) const {
                Entity entity = entity_manager_.getEntity(entityId);
                unpack(entity);
                return entity;
            }
            View<En, Components...>  _views;
            EntityManager& entity_manager_;
            std::tuple<ComponentHandle<Components> & ...> handles;
        };
    public:
        Entity createEntity() {
            entity_type entity = DefaultRegistry::create();
            return getEntity(entity);
        };

        Entity getEntity(entity_type entity) {

            entity_type pos = traits_type::entity_mask & entity;
            return Entity(entity, this);
            //if (!(pos < entityWrappers.size())) {
            //    entityWrappers.resize(pos + 1);
            //    entityWrappers[pos] = Entity(entity, this);
            //}
            //return entityWrappers[pos];
        };



        template <typename ... Components>
        std::enable_if_t<(sizeof...(Components) > 1), EntityViewMulti<uint32_t, Components...>>
            entities_with_components() {
            return EntityViewMulti<uint32_t, Components...>(*this);
        }

        template <typename ... Components>
        EntityViewSingle<uint32_t, Components...>
            entities_with_components() {
            return EntityViewSingle<uint32_t, Components...>(*this);
        }

        template <typename ... Components>
        std::enable_if_t<(sizeof...(Components) > 1), EntityComponentViewMulti<uint32_t, Components...>>
            entities_with_components(ComponentHandle<Components> & ... components) {
            return EntityComponentViewMulti<uint32_t, Components...>(*this, components...);
        }

        template <typename Components>
        EntityComponentViewSingle<uint32_t, Components> entities_with_components(ComponentHandle<Components> & components) {
            return EntityComponentViewSingle<uint32_t, Components>(*this, components);
        }

        template<typename Component>
        void receiveAddComponent(DefaultRegistry & entityManager, entity_type entity) {
            event_manager_.emit<ComponentAddedEvent<Component>>(getEntity(entity), ComponentHandle<Component>(this, entity));
        }

        template<typename Component>
        void receiveRemoveComponent(DefaultRegistry & entityManager, entity_type entity) {
            event_manager_.emit<ComponentRemovedEvent<Component>>(getEntity(entity), ComponentHandle<Component>(this, entity));
        }

        template <typename Event, typename Receiver>
        std::enable_if_t<std::is_same<typename Event, ComponentAddedEvent<typename Event::ComponentType>>::value>
            subscribe(Receiver& receiver)
        {
            using ComponentType = Event::ComponentType;
            event_manager_.subscribe<Event>(receiver);
            construction<ComponentType>().connect<EntityManager, &EntityManager::receiveAddComponent<ComponentType>>(this);
        }

        template <typename Event, typename Receiver>
        std::enable_if_t<std::is_same<typename Event, ComponentRemovedEvent<typename Event::ComponentType>>::value>
            subscribe(Receiver& receiver)
        {
            using ComponentType = Event::ComponentType;
            event_manager_.subscribe<Event>(receiver);
            destruction<ComponentType>().connect<EntityManager, &EntityManager::receiveRemoveComponent<ComponentType>>(this);
        }

        template <typename Event, typename Receiver>
        std::enable_if_t<std::is_same<typename Event, ComponentAddedEvent<typename Event::ComponentType>>::value>
            unsubscribe(Receiver& receiver)
        {
            using ComponentType = Event::ComponentType;
            event_manager_.unsubscribe<Event>(receiver);
            construction<ComponentType>().disconnect<EntityManager, &EntityManager::receiveAddComponent<ComponentType>>(this);
        }

        template <typename Event, typename Receiver>
        std::enable_if_t<std::is_same<typename Event, ComponentRemovedEvent<typename Event::ComponentType>>::value>
            unsubscribe(Receiver& receiver)
        {
            using ComponentType = Event::ComponentType;
            event_manager_.subscribe<Event>(receiver);
            destruction<ComponentType>().disconnect<EntityManager, &EntityManager::receiveRemoveComponent<ComponentType>>(this);
        }

        EntityManager(EventManager& events) :event_manager_(events) {}
        EventManager &event_manager_;
        std::vector<Entity> entityWrappers;
    };

    class BaseSystem : public entt::Family<struct SystemFamily> {
    public:
        typedef size_t Family;

        virtual ~BaseSystem() {};

        virtual void configure(EntityManager &entities, EventManager &events) {
            configure(events);
        }
        virtual void configure(EventManager &events) {}
        virtual void update(EntityManager &entities, EventManager &events, TimeDelta dt) = 0;
    };

    class SystemManager {
    public:
        SystemManager(EntityManager &entity_manager,
            EventManager &event_manager) :
            entity_manager_(entity_manager),
            event_manager_(event_manager) {}

        template <typename System>
        void add(std::shared_ptr<System> system) {
            systems_.insert(std::make_pair(System::type<System>(), system));
        }

        template <typename System, typename ... Args>
        std::shared_ptr<System> add(Args && ... args) {
            std::shared_ptr<System> s(new System(std::forward<Args>(args) ...));
            add(s);
            return s;
        }

        template <typename System>
        std::shared_ptr<System> system() {
            auto it = systems_.find(System::type<System>());
            assert(it != systems_.end());
            return it == systems_.end()
                ? std::shared_ptr<System>()
                : std::shared_ptr<System>(std::static_pointer_cast<System>(it->second));
        }

        template <typename System>
        void update(TimeDelta dt) {
            assert(initialized_ && "SystemManager::configure() not called");
            std::shared_ptr<System> s = system<System>();
            s->update(entity_manager_, event_manager_, dt);
        }

        void update_all(TimeDelta dt) {
            assert(initialized_ && "SystemManager::configure() not called");
            for (auto &pair : systems_) {
                pair.second->update(entity_manager_, event_manager_, dt);
            }
        };
        void configure() {
            for (auto &pair : systems_) {
                pair.second->configure(entity_manager_, event_manager_);
            }
            initialized_ = true;
        };

    private:
        bool initialized_ = false;
        EntityManager &entity_manager_;
        EventManager &event_manager_;
        std::unordered_map<BaseSystem::Family, std::shared_ptr<BaseSystem>> systems_;
    };


    template <typename Component>
    ComponentHandle<Component> Entity::component() const
    {
        return ComponentHandle<Component>(manager_, id_);
    }

    template <typename Component>
    Component& Entity::componentRaw() const
    {
        return manager_->get<Component>(id_);
    }


    template <typename Component>
    bool Entity::has_component() const
    {
        if (id_ != INVALID) {
            return manager_->has<Component>(id_);
        }
        return false;
    }
    template <typename Component, typename ... Args>
    ComponentHandle<Component> Entity::assign(Args && ... args)
    {
        if (id_ != INVALID)
        {
            //manager_->reserve<Component>(100);    
            manager_->assign<Component>(id_, std::forward<Args>(args) ...);
        }
        return  ComponentHandle<Component>(manager_, this->id());
    }

    template <typename Component>
    void Entity::remove()
    {
        if (id_ != INVALID)
        {
            manager_->remove<Component>(id_);
        }
    }

    inline  bool Entity::valid() const
    {
        if (id_ != INVALID)
        {
            return manager_->valid(id_);
        }
        return false;
    }

    inline entity_type Entity::id() const
    {
        return id_;
    }

    inline void Entity::destroy()
    {
        if (id_ != INVALID)
        {
            manager_->destroy(id_);
        }
    }

    template <typename C>
    inline ComponentHandle<C>::operator bool() const {
        return valid();
    }

    template <typename C>
    inline bool ComponentHandle<C>::valid() const {
        return manager_ && manager_->valid(id_) && manager_->has<C>(id_);
    }

    template <typename C>
    inline C *ComponentHandle<C>::operator -> () {
        assert(valid());
        return &manager_->get<C>(id_);
    }

    template <typename C>
    inline const C *ComponentHandle<C>::operator -> () const {
        assert(valid());
        return &manager_->get<C>(id_);
    }

    template <typename C>
    inline C &ComponentHandle<C>::operator * () {
        assert(valid());
        return manager_->get<C>(id_);
    }

    template <typename C>
    inline const C &ComponentHandle<C>::operator * () const {
        assert(valid());
        return manager_->get<C>(id_);
    }

    template <typename C>
    inline C *ComponentHandle<C>::get() {
        assert(valid());
        return &manager_->get<C>(id_);
    }

    template <typename C>
    inline const C *ComponentHandle<C>::get() const {
        assert(valid());
        return &manager_->get<C>(id_);
    }

    template <typename C>
    inline void ComponentHandle<C>::remove() {
        assert(valid());
        manager_->remove<C>(id_);
    }

    template <typename C>
    inline Entity ComponentHandle<C>::entity() {
        assert(valid());
        return manager_->getEntity(id_);
    }

    class EntityX {
    public:
        EntityX() : systems(entities, events),
            entities(events),
            events()
        {
        }

        EventManager events;
        EntityManager entities;
        SystemManager systems;


    };

}